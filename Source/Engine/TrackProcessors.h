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

#include "PanLaw.h"
#include "Capture.h"
#include "Loudness.h"
#include "Modulation.h"
#include "Playback.h"
#include "Swap.h"
#include "TimedPlugin.h"

#include <array>
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
    // How late the sound reaching it is after the song position the track's lanes
    // are drawn for: its plug-ins' delay (0096). Set by the engine.
    std::atomic<int> automationDelay { 0 };
    // Volume automation for this block, a level in dB for every sample (the
    // audio engine's document: curves followed sample by sample). Written by
    // the track's Source, which plays before the fader, and used once.
    std::vector<float> volumeCurve;
    std::atomic<int> volumeCurveSamples { 0 };
    std::vector<float> panCurve;               // the same for the pan lane, -1..1
    std::atomic<int> panCurveSamples { 0 };
    std::vector<std::uint8_t> muteCurve;       // and the mute lane, 1 muted (decision 0089)
    std::atomic<int> muteCurveSamples { 0 };
    std::atomic<float> automatedPan { std::numeric_limits<float>::quiet_NaN() };
    std::atomic<bool> audible { true }, phaseInvert { false }, mono { false };
    std::atomic<int> automatedMute { -1 };          // -1: not automated; 0 or 1 from a mute lane
    // Surround (decision 0040): the mix's format; a bus (folder, master) passes every
    // channel on, a track is placed among the speakers by its pan and front-back.
    std::atomic<int> format { 0 };
    std::atomic<bool> bus { false };
    std::atomic<float> frontBack { 1.0f };
    float currentGain() const { return lastGain.load(); }
    // The next block starts at the level set, not faded to it (a render's start).
    std::atomic<bool> snap { false };

    // The analysers' tap (decision 0044): the last 8192 samples out, for the window to read.
    static constexpr int tapSize = 8192;
    std::atomic<bool> tap { false };
    std::array<std::atomic<float>, tapSize> tapL {}, tapR {};
    std::atomic<int> tapWrite { 0 };
    // Copies the newest `n` samples (n <= tapSize) into l and r.
    void recent (float* l, float* r, int n) const
    {
        const int end = tapWrite.load();
        for (int i = 0; i < n; ++i)
        {
            const int k = (end - n + i + tapSize * 2) % tapSize;
            l[i] = tapL[(size_t) k].load (std::memory_order_relaxed);
            r[i] = tapR[(size_t) k].load (std::memory_order_relaxed);
        }
    }
    std::atomic<float> peakL { 0 }, peakR { 0 };   // since the window last read them
    std::atomic<float> rmsL { 0 }, rmsR { 0 };     // average level over the last 300 ms (decision 0038)
    std::atomic<float> level { 0 };                 // for envelope followers

    float takePeak (int channel) { return (channel == 0 ? peakL : peakR).exchange (0.0f); }

    // Loudness (decision 0033): measured on the master.
    std::atomic<bool> measureLoudness { false }, resetLoudness { false };
    loudness::Meter loudnessMeter;

    const juce::String getName() const override { return "Fader"; }
    void prepareToPlay (double rate, int block) override
    {
        volumeCurveSamples = 0;
        fresh = true;
        volumeCurve.assign ((size_t) juce::jmax (block, 8192), 0.0f);
        panCurve.assign ((size_t) juce::jmax (block, 8192), 0.0f);
        panCurveSamples = 0;
        muteCurve.assign ((size_t) juce::jmax (block, 8192), 0);
        muteCurveSamples = 0;
        smoothedGain.reset (rate, 0.02);
        for (auto& sp : speaker) sp.reset (rate, 0.02);
        loudnessMeter.prepare (rate);
    }
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
    double meanL = 0, meanR = 0;   // mean squares, smoothed
    std::array<juce::SmoothedValue<float>, 8> speaker;
    std::atomic<float> lastGain { 1.0f };
    bool fresh = true;   // the first block after preparing starts at the level set, not a fade to it
};

//==============================================================================
// Stands in for a switched-off plug-in (decision 0086): the sound delayed as
// long as the plug-in delayed it, and saying so, so delay compensation still
// lines everything up.
class HeldDelay : public juce::AudioProcessor
{
public:
    explicit HeldDelay (int samples)
        : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo()).withOutput ("Out", juce::AudioChannelSet::stereo())),
          length (juce::jmax (0, samples))
    {
        setLatencySamples (length);
    }
    const int length;
    const juce::String getName() const override { return "Switched-off Plug-in's Delay"; }
    void prepareToPlay (double, int) override { ring.setSize (2, length + 1); ring.clear(); at = 0; }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override
    {
        m.clear();
        const int size = ring.getNumSamples();
        if (size < 2) return;
        int w = at;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            for (int ch = 0; ch < juce::jmin (2, b.getNumChannels()); ++ch)
            {
                ring.setSample (ch, w, b.getSample (ch, i));
                b.setSample (ch, i, ring.getSample (ch, (w + 1) % size));
            }
            w = (w + 1) % size;
        }
        at = w;
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
    juce::AudioBuffer<float> ring;
    int at = 0;
};

//==============================================================================
class SendGain : public juce::AudioProcessor
{
public:
    SendGain() : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo())
                                                  .withOutput ("Out", juce::AudioChannelSet::stereo())) {}
    std::atomic<float> gainDb { -6 };
    std::atomic<float> automatedDb { std::numeric_limits<float>::quiet_NaN() };   // from a send lane
    std::atomic<int> automationDelay { 0 };    // as the fader's (0096)
    std::atomic<bool> on { true };
    std::atomic<Fader*> follow { nullptr };   // in a surround mix, a post-fader send follows this fader's level
    std::atomic<bool> snap { false };          // the next block starts at the level set
    std::vector<float> curve;                  // its lane, dB at every sample of the block (0089)
    std::atomic<int> curveSamples { 0 };

    const juce::String getName() const override { return "Send"; }
    void prepareToPlay (double, int block) override
    {
        last = std::numeric_limits<float>::quiet_NaN();
        curve.assign ((size_t) juce::jmax (block, 8192), 0.0f);
        curveSamples = 0;
    }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& m) override
    {
        m.clear();
        const float autoDb = automatedDb.load();
        const float db = std::isnan (autoDb) ? gainDb.load() : autoDb;
        if (snap.exchange (false)) last = std::numeric_limits<float>::quiet_NaN();
        const float followed = follow.load() != nullptr ? std::abs (follow.load()->currentGain()) : 1.0f;
        const int n = b.getNumSamples();
        if (curveSamples.exchange (0) == n && ! std::isnan (autoDb) && n <= (int) curve.size())
        {
            // Its lane, sample by sample (0089).
            for (int i = 0; i < n; ++i)
            {
                const float gi = on ? juce::Decibels::decibelsToGain (curve[(size_t) i], -100.0f) * followed : 0.0f;
                for (int ch = 0; ch < b.getNumChannels(); ++ch) b.getWritePointer (ch)[i] *= gi;
            }
            last = on ? juce::Decibels::decibelsToGain (curve[(size_t) n - 1], -100.0f) * followed : 0.0f;
            return;
        }
        float g = on ? juce::Decibels::decibelsToGain (db, -100.0f) : 0.0f;
        g *= followed;
        if (std::isnan (last)) last = g;   // the first block starts where it is: no fade in from elsewhere
        if (last == g) { if (g != 1.0f) b.applyGain (g); }
        else b.applyGainRamp (0, b.getNumSamples(), last, g);
        last = g;
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
    float last = std::numeric_limits<float>::quiet_NaN();
};

//==============================================================================
// One audio region's samples added (at 64 bits) to l and r, which hold the
// timeline's samples first .. first + length. Each sample of the timeline
// belongs to one region at most: the one whose edges, each rounded to its
// nearest sample, hold it, so two halves of a split meet on one sample.
void mixRegion (const playback::AudioRegion&, const juce::AudioBuffer<float>&, double rate,
                long long first, int length, double* l, double* r);

class TrackSource : public juce::AudioProcessor, public MidiTap
{
public:
    TrackSource (Shared&, Id track);
    ~TrackSource() override;

    struct ParamLane
    {
        AutomationLane lane;
        juce::AudioProcessorParameter* param = nullptr;
        TimedPlugin* host = nullptr;   // the plug-in's wrapper, which cuts its blocks where the lane says (0089)
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
        bool hasVolume = false, hasPan = false, hasMute = false;
        AutomationLane volume, pan, mute;
        std::vector<std::pair<AutomationLane, SendGain*>> sends;   // send levels, automated
        std::vector<ModulatorRun> modulators;
        TempoMap tempo;
        bool audible = true;          // false when muted: notes still stop cleanly
        bool monitor = false;         // pass the live input on
        bool armed = false;
        bool armedMidi = false;       // armed for MIDI (decision 0064)
        bool readAutomation = true;
        bool summing = false;         // a Summing track: silent while it records the master (decision 0052)
        Fader* fader = nullptr;
    };

    Swap<Data> data;

    // Recording (message thread starts and stops; audio thread writes).
    void startRecording (const juce::File&, double rate);
    juce::File stopRecording();
    bool isRecording() const { return writer != nullptr; }
    double recordStartSeconds() const { return recordStart; }
    // What reaches this source arrives this late after the song position it
    // belongs to: a Summing track's recorder hears the master after the
    // graph's delay compensation (decision 0052).
    std::atomic<double> recordLatencySeconds { 0 };

    // The last minute of the live input, for Capture Audio.
    void keepInput (bool);
    juce::AudioBuffer<float> recentInput (double seconds) const;

    // MIDI played in while recording: (song ticks, message) pairs, read by the window.
    struct Played { double ticks; std::uint8_t bytes[3]; };
    juce::AbstractFifo playedFifo { 8192 };
    std::array<Played, 8192> played {};

    // Armed for MIDI (decision 0064): what the track's instrument is played
    // (or, if it sends MIDI of its own, what it sends), caught on the device
    // the engine taps, with its place in the song in seconds; read by the window.
    struct Tapped { double seconds; std::uint8_t bytes[3]; bool fromOutput; };
    juce::AbstractFifo tappedFifo { 16384 };
    std::array<Tapped, 16384> tapped {};
    std::atomic<bool> recordOutput { false };
    void caught (const juce::MidiBuffer&, bool fromOutput) override;

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
    // Note chance, velocity and timing (decision 0061): rolled as each note
    // comes up; a note played late waits here, in a list that never grows.
    playback::Dice dice;
    struct Waiting { double seconds; std::uint8_t bytes[3]; };
    std::array<Waiting, 256> waiting {};
    int waitingCount = 0;
    std::array<std::uint16_t, 16 * 128> skipped {};    // note-ons not played this time, whose note-offs are dropped too
    void emit (const Data&, const std::uint8_t* bytes, int size, int at, juce::MidiBuffer&);
    std::array<modulation::Runner, 32> runners;
    std::array<Id, 32> runnerIds {};
    juce::AudioBuffer<float> live;
    std::vector<double> mixL, mixR;   // the clips mixed at 64 bits, a block long
    std::vector<double> blockTicks;   // the song's ticks at each sample of the block (0089)
    // Where the song was, block by block, so a lane can be read for sound that
    // arrives later than it was played (0096): from sample `from` (counted since
    // the start) the song is at `seconds`, moving on in real time. A new span
    // only where the song jumped or looped; kept in a fixed ring.
    struct Span { juce::int64 from = 0; double seconds = 0; };
    std::array<Span, 64> spans {};
    int spanCount = 0, spanNewest = -1;
    juce::int64 samplesPlayed = 0;
    std::vector<double> lateTicks;    // a lane's ticks for sound arriving late
    int lateFor = -1;                 // the delay lateTicks was made for, this block
    void noteSpans (int n);
    double secondsAt (juce::int64 sample) const;
    const std::vector<double>& ticksFor (const Data&, int delay, int n);
    std::uint32_t drawn = 0;
    void drawAutomation (const Data&, int samples);

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
