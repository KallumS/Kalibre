/*
    ContainerProcessor - Kalibre's Container device (decision 0055).

    Up to eight plug-ins, one after another, each with:

      - On and Solo (with anything soloed, only what's soloed plays);
      - Stereo, Mid or Side: the plug-in hears and changes only the middle or
        only the sides, the rest passes by; or Mono (0118): it hears both sides
        as one, and what it sends back is one;
      - a band (Low to High): the plug-in hears and changes only that part of
        the spectrum, the rest passes by. The band is split off with
        Linkwitz-Riley filters, as multiband processors do: below, in and
        above the band add up to the input's level at every frequency, so a
        plug-in that does nothing leaves every frequency as loud as it was;
      - its own volume and pan, on what it changed.

    What passes by is held back by the plug-in's own delay, so it lines up
    with what the plug-in sends back: nothing thins or combs. The container's
    delay is all of theirs added up, and doesn't change when one is switched
    off or soloed.

    Match Loudness: the container measures the short-term loudness (LUFS-S,
    K-weighted, three seconds, BS.1770) of what comes in and what goes out,
    and turns what goes out up or down, slowly, until they match.

    Every setting is a parameter, so it can be automated, modulated and
    picked up by "last touched". The plug-ins inside are kept in the
    container's own state.
*/

#pragma once

#include "Loudness.h"
#include "Model.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace kal
{

class PluginManager;

PluginRef containerRef();

class ContainerProcessor : public juce::AudioPluginInstance
{
public:
    static constexpr int slotCount = 8;
    enum class Width { Stereo, Mid, Side };

    explicit ContainerProcessor (PluginManager&);
    ~ContainerProcessor() override;

    struct Slot
    {
        PluginRef ref;                                   // no format: an empty place
        std::unique_ptr<juce::AudioPluginInstance> plugin;   // null when its plug-in couldn't be loaded

        juce::AudioParameterBool* on = nullptr;
        juce::AudioParameterBool* solo = nullptr;
        juce::AudioParameterChoice* width = nullptr;
        juce::AudioParameterFloat* low = nullptr;        // Hz; 20 is no low edge
        juce::AudioParameterFloat* high = nullptr;       // Hz; 20 000 is no high edge
        juce::AudioParameterFloat* gainDb = nullptr;
        juce::AudioParameterFloat* pan = nullptr;
        // Mono (0118): its own parameter, made after every other, so songs saved before keep
        // their Stereo, Mid and Side (choices are saved as fractions) and their automation.
        juce::AudioParameterBool* mono = nullptr;

        // The audio thread's.
        juce::AudioBuffer<float> held;                   // what passes by, held back by the plug-in's delay
        int heldAt = 0, latency = 0;
        struct Filters;                                  // the band's Linkwitz-Riley split
        std::shared_ptr<Filters> filters;

        bool used() const { return ! ref.format.empty(); }
        bool hasLowEdge() const { return low->get() > 20.5f; }
        bool hasHighEdge() const { return high->get() < 19900.0f; }
    };

    // Message thread.
    bool loadPlugin (int slot, const PluginRef&, juce::String& error);
    void clearSlot (int slot);
    void moveSlot (int from, int to);
    const Slot& slot (int i) const { return slots[(size_t) i]; }
    Slot& slot (int i) { return slots[(size_t) i]; }
    int firstEmpty() const;   // slotCount when full

    juce::AudioParameterBool* matchLoudness = nullptr;
    std::atomic<float> appliedGainDb { 0 };   // what Match Loudness is doing now

    // Shown by the app: choose a plug-in for a place (set by the device rack).
    static std::function<void (std::function<void (const PluginRef&)>)> choosePlugin;
    std::function<void()> onSlotsChanged;

    //==========================================================================
    const juce::String getName() const override { return "Container"; }
    void fillInPluginDescription (juce::PluginDescription&) const override;
    void prepareToPlay (double, int) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override;
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

private:
    PluginManager& plugins;
    std::array<Slot, slotCount> slots;
    juce::CriticalSection slotLock;   // taken by the audio thread only with tryEnter

    juce::AudioBuffer<float> toPlugin, passing, input;
    juce::MidiBuffer noMidi;
    loudness::Meter inMeter, outMeter;
    double rate = 48000;
    int block = 512;
    float compensationDb = 0;

    void prepareSlot (Slot&);
    void updateLatency();
    void processSlot (Slot&, juce::AudioBuffer<float>&, int n, bool active);
};

} // namespace kal
