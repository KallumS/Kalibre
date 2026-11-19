/*
    HostedProcessor - a JUCE plug-in's processor, compiled into Kalibre, as a
    device (decision 0067): Instant Virtual Instrument.

    Kalibre's devices are plug-in instances; a plug-in's own source makes a
    plain AudioProcessor. This wraps one: its buses, sound, MIDI, state and
    latency pass straight through; its parameters are passed on one for one
    (so automation, last touched and MIDI learn reach them); and its window
    sits inside a window of the device's own, which closes it as Kalibre
    closes windows (the plug-in told first, decision 0056).
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace kal
{

class HostedProcessor : public juce::AudioPluginInstance
{
public:
    HostedProcessor (std::unique_ptr<juce::AudioProcessor>, juce::PluginDescription);
    ~HostedProcessor() override;

    juce::AudioProcessor& inner() { return *processor; }
    // The MIDI it leaves in the buffer goes on to what follows, though the plug-in says
    // it sends none: ScaleView reads the notes and passes them by (decision 0093).
    bool passMidiOn = false;

    const juce::String getName() const override { return description.name; }
    void fillInPluginDescription (juce::PluginDescription& d) const override { d = description; }
    void prepareToPlay (double rate, int block) override;
    void releaseResources() override { processor->releaseResources(); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void setPlayHead (juce::AudioPlayHead* h) override { AudioPluginInstance::setPlayHead (h); processor->setPlayHead (h); }
    void setNonRealtime (bool offline) noexcept override { AudioPluginInstance::setNonRealtime (offline); processor->setNonRealtime (offline); }
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l == processor->getBusesLayout(); }
    double getTailLengthSeconds() const override { return processor->getTailLengthSeconds(); }
    bool acceptsMidi() const override { return processor->acceptsMidi(); }
    bool producesMidi() const override { return passMidiOn || processor->producesMidi(); }
    bool isMidiEffect() const override { return processor->isMidiEffect(); }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return processor->hasEditor(); }
    int getNumPrograms() override { return processor->getNumPrograms(); }
    int getCurrentProgram() override { return processor->getCurrentProgram(); }
    void setCurrentProgram (int i) override { processor->setCurrentProgram (i); }
    const juce::String getProgramName (int i) override { return processor->getProgramName (i); }
    void changeProgramName (int i, const juce::String& n) override { processor->changeProgramName (i, n); }
    void getStateInformation (juce::MemoryBlock& m) override { processor->getStateInformation (m); }
    void setStateInformation (const void* d, int n) override { processor->setStateInformation (d, n); }

private:
    static BusesProperties busesOf (const juce::AudioProcessor&);
    std::unique_ptr<juce::AudioProcessor> processor;
    juce::PluginDescription description;
};

} // namespace kal
