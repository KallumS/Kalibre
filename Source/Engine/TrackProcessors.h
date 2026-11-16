/*
    TrackProcessors - the engine's own nodes in the audio graph.

    Every track is a chain of nodes (docs/ARCHITECTURE.md):

        input ──► Source ──► MIDI effects ──► instruments ─┐
                    └──────── audio from clips ────────────┴─► audio effects ─► Fader ─► folder / master
                                                                                 └─► Sends ─► returns

    The Source plays the track's clips (MIDI and audio on one track,
    decision 0003), passes on what comes in live when the track is armed or
    chosen, records, applies automation and modulators to the track's
    plug-ins, and keeps the last minute of input for Capture Audio. The Fader
    does volume, pan, mute and solo, polarity and mono, and meters.

    The audio thread never allocates or locks here: new data arrives through
    Swap.
*/

#pragma once

#include "Capture.h"
#include "Loudness.h"
#include "Modulation.h"
#include "Playback.h"
#include "Swap.h"
#include "Warp.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace kal
{

//==============================================================================
// What every node needs to know about time, written by the engine at the
// start of each block on the audio thread, read by the nodes in that block.
struct Clock
{
    bool playing = false;
    bool recording = false;
    double start = 0;          // seconds at the block's first sample
    int samples = 0;
    int wrapAt = -1;           // the loop wraps at this sample, or -1
    double wrapTo = 0;         // to this many seconds
    bool jumped = false;       // the position moved by itself (a locate): stop held notes
    double rate = 48000;
    double ppq = 0;            // quarter notes at the block's start
    double bpm = 120;

    double secondsAt (int sample) const
    {
        if (wrapAt >= 0 && sample >= wrapAt) return wrapTo + (sample - wrapAt) / rate;
        return start + sample / rate;
    }
};

struct Shared
{
    Clock clock;                                  // audio thread
    std::atomic<bool> anySolo { false };
    capture::Ring<16384> midiCapture;             // every MIDI message from a keyboard
    std::atomic<double> songTicks { -1 };         // for Capture: where the playhead is, -1 stopped
};

//==============================================================================
class Fader : public juce::AudioProcessor
{
public:
    Fader();

    std::atomic<float> volumeDb { 0 }, pan { 0 };
    std::atomic<float> automatedVolume { std::numeric_limits<float>::quiet_NaN() };
    std::atomic<float> automatedPan { std::numeric_limits<float>::quiet_NaN() };
    std::atomic<bool> audible { true }, phaseInvert { false }, mono { false };
    std::atomic<float> peakL { 0 }, peakR { 0 };   // since the window last read them
    std::atomic<float> level { 0 };                 // for envelope followers

    float takePeak (int channel) { return (channel == 0 ? peakL : peakR).exchange (0.0f); }

    // Loudness (decision 0033): measured on the master.
    std::atomic<bool> measureLoudness { false }, resetLoudness { false };
    loudness::Meter loudnessMeter;

    const juce::String getName() const override { return "Fader"; }
    void prepareToPlay (double rate, int) override { smoothedGain.reset (rate, 0.02); loudnessMeter.prepare (rate); }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    juce::SmoothedValue<float> smoothedGain { 1.0f }, smoothedL { 1.0f }, smoothedR { 1.0f };
};

//==============================================================================
class SendGain : public juce::AudioProcessor
{
public:
    SendGain() : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo())
                                                  .withOutput ("Out", juce::AudioChannelSet::stereo())) {}
    std::atomic<float> gainDb { -6 };
    std::atomic<bool> on { true };

    const juce::String getName() const override { return "Send"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override
    {
        m.clear();
        b.applyGainRamp (0, b.getNumSamples(), last, on ? juce::Decibels::decibelsToGain (gainDb.load(), -100.0f) : 0.0f);
        last = on ? juce::Decibels::decibelsToGain (gainDb.load(), -100.0f) : 0.0f;
    }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    float last = 0.5f;
};

//==============================================================================
class TrackSource : public juce::AudioProcessor
{
public:
    TrackSource (Shared&, Id track);
    ~TrackSource() override;

    struct ParamLane
    {
        AutomationLane lane;
        juce::AudioProcessorParameter* param = nullptr;
    };

    struct ModulatorRun
    {
        Modulator mod;
        std::vector<juce::AudioProcessorParameter*> params;   // one per target
        std::vector<float> bases;                             // where each sat before modulation
        std::atomic<float>* followLevel = nullptr;            // the followed track's level
    };

    struct Data
    {
        playback::CompiledTrack compiled;
        std::vector<std::shared_ptr<const CachedAudio>> regionAudio;
        std::vector<ParamLane> params;
        bool hasVolume = false, hasPan = false;
        AutomationLane volume, pan;
        std::vector<ModulatorRun> modulators;
        TempoMap tempo;
        bool audible = true;          // false when muted: notes still stop cleanly
        bool monitor = false;         // pass the live input on
        bool armed = false;
        bool readAutomation = true;
        Fader* fader = nullptr;
    };

    Swap<Data> data;

    // Recording (message thread starts and stops; audio thread writes).
    void startRecording (const juce::File&, double rate);
    juce::File stopRecording();
    bool isRecording() const { return writer != nullptr; }
    double recordStartSeconds() const { return recordStart; }

    // The last minute of the live input, for Capture Audio.
    void keepInput (bool);
    juce::AudioBuffer<float> recentInput (double seconds) const;

    // MIDI played in while recording: (song ticks, message) pairs, read by the window.
    struct Played { double ticks; std::uint8_t bytes[3]; };
    juce::AbstractFifo playedFifo { 8192 };
    std::array<Played, 8192> played {};

    // Last-touched: a parameter of this track's plug-ins being written by a hand.
    Id trackId;

    const juce::String getName() const override { return "Source"; }
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    Shared& shared;
    size_t nextMidi = 0;
    double lastEnd = -1;
    std::array<std::uint16_t, 16 * 128> sounding {};   // notes we started and haven't ended
    std::array<modulation::Runner, 32> runners;
    std::array<Id, 32> runnerIds {};
    juce::AudioBuffer<float> live;

    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    juce::TimeSliceThread writerThread { "Kalibre recording" };
    double recordStart = 0;
    juce::File recordFile;
    std::atomic<bool> writing { false };

    juce::AudioBuffer<float> history;
    std::atomic<int> historyWrite { 0 };
    std::atomic<bool> keeping { false };

    void playSegment (const Data&, double from, double to, int offset, int length,
                      juce::AudioBuffer<float>&, juce::MidiBuffer&);
    void stopAll (juce::MidiBuffer&, int at);
    void applyAutomation (const Data&, double seconds);
};

//==============================================================================
class Metronome : public juce::AudioProcessor
{
public:
    explicit Metronome (Shared& s) : AudioProcessor (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo())), shared (s) {}

    struct Settings { MetronomeSettings m; TempoMap tempo; bool countingIn = false; };
    Swap<Settings> settings;

    const juce::String getName() const override { return "Metronome"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    // Clicks are forced before this many seconds (a count-in).
    std::atomic<double> countInUntil { -1.0e9 };

private:
    Shared& shared;
    double phase = 0;
    int clickLeft = 0;
    bool accent = false;
};

} // namespace kal
