/*
    NoteSplitter - the Note Splitter, a Kalibre MIDI effect (decision 0083):
    a chord's notes, top to bottom, each to another track's MIDI input. With
    four outputs, a four-note chord's top note goes to the track on output 1,
    the next down to output 2, the bottom to output 4 (Core/NoteSplit's rule).

    Which track each output goes to is the song's (Device::midiTo), set in the
    device's panel; the engine gives each output a node of its own
    (NoteSplitOutput) between the splitter and that track's Source, which
    takes what arrives as MIDI played in: heard, and recorded if the track is
    armed for MIDI. Everything the splitter hears goes on unchanged to its own
    track's instruments too.

    An output's node reads the messages the splitter routed this block; the
    graph plays it after the splitter (it hangs off the splitter's MIDI), so
    nothing is shared while both play (0078).
*/

#pragma once

#include "Model.h"
#include "NoteSplit.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>

namespace kal
{

PluginRef noteSplitterRef();

class NoteSplitterProcessor : public juce::AudioPluginInstance
{
public:
    NoteSplitterProcessor();

    std::atomic<int> outputCount { 4 };   // the engine keeps it to the song's
    std::atomic<Id> deviceId { 0 };       // which device it is, for its panel

    // What it routed in the last block it played, and which block that was.
    std::vector<NoteSplit::Message> routed;
    std::atomic<std::uint32_t> played { 0 };

    // The panel (the window's, which knows the song's tracks); set by the app.
    static std::function<juce::AudioProcessorEditor* (NoteSplitterProcessor&)> makeEditor;

    const juce::String getName() const override { return "Note Splitter"; }
    void fillInPluginDescription (juce::PluginDescription&) const override;
    void prepareToPlay (double rate, int block) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return makeEditor ? makeEditor (*this) : nullptr; }
    bool hasEditor() const override { return makeEditor != nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    NoteSplit split;
    double rate = 48000;
    juce::int64 samples = 0;   // a clock that keeps going, for what counts as one chord
    bool wasPlaying = false;
};

// One output of a Note Splitter: the messages for it, into a track's MIDI input.
class NoteSplitOutput : public juce::AudioProcessor
{
public:
    NoteSplitOutput() : AudioProcessor (BusesProperties()) {}
    void set (NoteSplitterProcessor* from, int output) { source.store (from); index.store (output); }

    const juce::String getName() const override { return "Note Splitter Output"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
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
    std::atomic<NoteSplitterProcessor*> source { nullptr };
    std::atomic<int> index { 0 };
    std::uint32_t seen = 0;
    std::array<bool, 16 * 128> sounding {};
};

} // namespace kal
