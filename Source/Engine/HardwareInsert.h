/*
    HardwareInsert - an outboard box in a track's chain (decision 0040).

    The sound goes out of two of the interface's outputs, through the
    hardware, and back in through two inputs. Its round trip (the
    interface's latency both ways and the converters) is measured with
    Ping: a click goes out and Kalibre listens for it coming back. That
    delay is the insert's latency, so plug-in delay compensation lines the
    rest of the song up with it, like any plug-in.

    The audio callback hands the interface's inputs to every insert before
    the graph runs and adds what they send to its outputs after
    (HardwareBus). When exporting there's no interface: the insert passes
    the sound through untouched, and says so.
*/

#pragma once

#include "Model.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace kal
{

// The interface's channels for this block, shared with every insert (audio thread).
struct HardwareBus
{
    const float* const* inputs = nullptr;
    int numInputs = 0;
    juce::AudioBuffer<float> sends;   // what inserts send, by output channel
    int numOutputs = 0;
    int samples = 0;
    bool live = false;                // false while rendering: inserts pass through

    static HardwareBus& get();
};

PluginRef hardwareInsertRef();

class HardwareInsert : public juce::AudioPluginInstance
{
public:
    HardwareInsert();

    enum P { SendPair, ReturnPair, Latency, InputGain, Count };

    const juce::String getName() const override { return "Hardware Insert"; }
    void fillInPluginDescription (juce::PluginDescription&) const override;
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::RangedAudioParameter* param (int i) const { return params[(size_t) i]; }
    int pair (int which) const;      // 0-based channel pair
    void ping();                     // measure the round trip
    std::atomic<int> pingResult { -2 };   // -2 none yet, -1 heard nothing, else samples
    bool pinging() const { return pingState.load() != 0; }

private:
    std::vector<juce::RangedAudioParameter*> params;
    std::atomic<int> pingState { 0 };   // 0 idle, 1 asked, 2 listening
    juce::int64 pingSent = 0, sampleClock = 0;
    double rate = 48000;
    void applyLatency();
};

} // namespace kal
