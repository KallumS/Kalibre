/*
    MorphProcessor - the X/Y pad device (decision 0018).

    Four corners, each a plug-in or a sample. Drag the point between them and
    what you hear morphs: plug-in corners are all fed the same sound and MIDI
    and mixed by the pad's weights (VST-Morpher's arithmetic); sample corners
    are played by the track's notes, pitched from middle C, and mixed the
    same way. X and Y are ordinary parameters, so they can be automated,
    modulated and picked up by "last touched".

    Every corner always runs and is held back to line up with the slowest
    one, so a morph never combs (VST-Morpher's decision 0005).
*/

#pragma once

#include "Model.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace kal
{

class PluginManager;

std::unique_ptr<juce::AudioPluginInstance> makeBuiltIn (const PluginRef&, PluginManager&);
PluginRef morphRef();

// X and Y: plain 0-1 parameters a host can automate.
class MorphParameter : public juce::HostedAudioProcessorParameter
{
public:
    MorphParameter (juce::String i, juce::String n, float v) : id (std::move (i)), name (std::move (n)), value (v), initial (v) {}
    float get() const { return value.load(); }
    void operator= (float v) { value = juce::jlimit (0.0f, 1.0f, v); }
    float getValue() const override { return value.load(); }
    void setValue (float v) override { value = juce::jlimit (0.0f, 1.0f, v); }
    float getDefaultValue() const override { return initial; }
    juce::String getName (int max) const override { return name.substring (0, max); }
    juce::String getLabel() const override { return {}; }
    juce::String getText (float v, int) const override { return juce::String (juce::roundToInt (v * 100)) + "%"; }
    float getValueForText (const juce::String& t) const override { return t.getFloatValue() / 100.0f; }
    juce::String getParameterID() const override { return id; }

private:
    juce::String id, name;
    std::atomic<float> value;
    float initial;
};

class MorphProcessor : public juce::AudioPluginInstance
{
public:
    explicit MorphProcessor (PluginManager&);
    ~MorphProcessor() override;

    struct Corner
    {
        enum class Kind { Empty, Plugin, Sample };
        Kind kind = Kind::Empty;
        PluginRef ref;
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        juce::File file;
        juce::AudioBuffer<float> sample;
        double sampleRate = 48000;
        juce::String name() const;
    };

    // Message thread.
    bool loadPlugin (int corner, const PluginRef&, juce::String& error);
    bool loadSample (int corner, const juce::File&, juce::String& error);
    void clearCorner (int corner);
    const Corner& corner (int i) const { return corners[(size_t) i]; }

    MorphParameter* x = nullptr;
    MorphParameter* y = nullptr;

    // Shown by the app: choose a plug-in for a corner (set by the device rack).
    static std::function<void (std::function<void (const PluginRef&)>)> choosePlugin;

    //==========================================================================
    const juce::String getName() const override { return "X/Y Morph"; }
    void fillInPluginDescription (juce::PluginDescription&) const override;
    void prepareToPlay (double, int) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 2.0; }
    bool acceptsMidi() const override { return true; }
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

    std::function<void()> onCornersChanged;

    // A corner's plug-in in a window of its own (asked for, 0114): opened when it's
    // loaded from the device's window, and by clicking its letter; never inside the
    // device's own window. Message thread.
    void showCornerWindow (int corner);
    void closeCornerWindow (int corner);
    bool cornerWindowOpen (int corner) const { return windows[(size_t) corner] != nullptr; }
    juce::DocumentWindow* cornerWindow (int corner) { return windows[(size_t) corner].get(); }   // tests
    std::function<void()> onWindowsChanged;

private:
    PluginManager& plugins;
    std::array<Corner, 4> corners;
    std::array<std::unique_ptr<juce::DocumentWindow>, 4> windows;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    juce::CriticalSection cornerLock;   // taken by the audio thread only with tryEnter

    struct Voice { int corner = -1; int note = -1; double pos = 0, step = 1; float gain = 0; bool releasing = false; float fade = 1; };
    std::array<Voice, 32> voices;

    juce::AudioBuffer<float> scratch, sum;
    juce::MidiBuffer scratchMidi;
    std::array<juce::AudioBuffer<float>, 4> delayLines;
    std::array<int, 4> delayWrite {};
    double rate = 48000;
    int block = 512;

    void updateLatency();
    void renderSamples (juce::AudioBuffer<float>&, const juce::MidiBuffer&, const std::array<float, 4>& w);
};

} // namespace kal
