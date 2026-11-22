/*
    SplitProcessor - what Kalibre's splitting devices share: the Multiband
    (0126) and Stereo Split (0127).

    The sound cut into parts (bands of frequencies; or what's left, in the
    middle and right; or the middle and the sides), each part with up to four
    plug-ins one after another, On, Solo and its own volume, and the parts
    added back up, at 64 bits (0081). A part's plug-ins' delay holds the other
    parts back to meet it. The split itself, and its own delay, are the
    device's: `split` fills each part's buffer so that, with nothing done to
    them, they add up to the input (held back by `splitLatency`).

    With nothing to do (no plug-in, every part on, at 0 dB, none soloed) the
    input passes untouched, held back only by the split's own delay (exactly:
    a cancellation test).

    The parts' plug-ins open in windows of their own (as an X/Y Morph
    corner's, 0114). Every setting is a parameter; the plug-ins are kept in the
    device's own state.
*/

#pragma once

#include "Model.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>

namespace kal
{

class PluginManager;

class SplitProcessor : public juce::AudioPluginInstance
{
public:
    static constexpr int maxParts = 4, perPart = 4;

    SplitProcessor (PluginManager&, const juce::String& deviceName, const juce::String& identifier);
    ~SplitProcessor() override;

    struct Slot
    {
        PluginRef ref;                                       // no format: an empty place
        std::unique_ptr<juce::AudioPluginInstance> plugin;   // null when its plug-in couldn't be loaded
        bool used() const { return ! ref.format.empty(); }
    };

    struct Part
    {
        std::array<Slot, perPart> slots;
        juce::AudioParameterBool* on = nullptr;
        juce::AudioParameterBool* solo = nullptr;
        juce::AudioParameterFloat* gainDb = nullptr;
        // The audio thread's.
        juce::AudioBuffer<float> buffer, held;
        int heldAt = 0, latency = 0;
        bool hasPlugins() const { for (auto& s : slots) if (s.plugin) return true; return false; }
        int used() const { int n = 0; for (auto& s : slots) n += s.used() ? 1 : 0; return n; }
    };

    // The parts in play, in the order shown (no allocation: the audio thread asks too).
    struct InUse
    {
        std::array<int, maxParts> at {};
        int count = 0;
        const int* begin() const { return at.data(); }
        const int* end() const { return at.data() + count; }
    };
    virtual InUse partsInUse() const = 0;
    virtual juce::String partName (int) const = 0;
    virtual juce::String partDetail (int) const { return {}; }   // under its name: its range
    const Part& part (int i) const { return partsArray[(size_t) i]; }

    // Message thread.
    bool loadPlugin (int part, int slot, const PluginRef&, juce::String& error);
    void clearSlot (int part, int slot);

    // A part's plug-in in a window of its own (as an X/Y Morph corner's, 0114).
    void showWindow (int part, int slot);
    void closeWindow (int part, int slot);
    void closeWindows();
    bool windowOpen (int part, int slot) const { return windows[(size_t) (part * perPart + slot)] != nullptr; }
    juce::DocumentWindow* window (int part, int slot) { return windows[(size_t) (part * perPart + slot)].get(); }

    std::function<void()> onChanged;
    // Shown by the app: choose an audio effect for a part (set by the device rack).
    static std::function<void (std::function<void (const PluginRef&)>)> choosePlugin;

    //==========================================================================
    const juce::String getName() const override { return deviceName; }
    void fillInPluginDescription (juce::PluginDescription&) const override;
    void prepareToPlay (double, int) override;
    void releaseResources() override;
    void reset() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    double getTailLengthSeconds() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

protected:
    // A part's On, Solo and Volume, as "<id>on", "<id>solo", "<id>gain", named "<name> On"...
    void addPartParameters (int part, const juce::String& id, const juce::String& name);
    // Fill each part in use's `buffer` (n samples) from the input (left as it is).
    virtual void split (const juce::AudioBuffer<float>& input, int n) = 0;
    virtual int splitLatency() const { return 0; }
    virtual void prepareSplit (double /*rate*/, int /*block*/) {}   // under the lock
    virtual void resetSplit() {}                                   // under the lock
    virtual void listen (const juce::AudioBuffer<float>&, int) {}   // the input, for a picture
    virtual void stateLoaded() {}

    void updateLatency();
    void changed();

    PluginManager& plugins;
    const juce::String deviceName, identifier;
    std::array<Part, maxParts> partsArray;
    juce::CriticalSection lock;   // the audio thread only tries it
    double rate = 48000;
    int block = 512;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

private:
    std::array<std::unique_ptr<juce::DocumentWindow>, maxParts * perPart> windows;
    juce::AudioBuffer<float> dry;   // the input held back by the split's delay, for when nothing's done
    int dryAt = 0;
    void prepareSlot (Slot&);
};

// One part's column for a splitting device's editor: its name and range, On,
// Solo, its volume, and its plug-ins (Add Plug-in…; click for its window,
// right-click to replace or remove it).
std::unique_ptr<juce::Component> makePartColumn (SplitProcessor&, int part);

} // namespace kal
