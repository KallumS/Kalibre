/*
    DrumMachine - Kalibre Drums (decision 0105): a kit of pads, each a sample
    or a plug-in (a drum synth), each played by its note; and a step pattern
    it plays with the song.

    Notes played to it (a clip's, a keyboard's) play the pads on those notes.
    Its own pattern plays while the song plays, from the song's start, round
    the loop on the very sample, swung as the drum grid swings; it's the drum
    grid's pattern (patterns::Grid), so dragged out to the timeline it's a
    clip the drum grid opens.

    Each pad's sample is read into memory at the device's rate when chosen; a
    pad's plug-in is hosted inside, as the Container hosts its plug-ins, sent
    the pad's plug-in note for each hit and its note-off after the step. Pads
    in a choke group stop each other (the closed hat stops the open one).

    The kit and pattern, and each plug-in's settings, are the device's saved
    state. Edits in its window go through `edit`, which the app makes one
    undo step each.
*/

#pragma once

#include "DrumKit.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>
#include <map>

namespace kal
{

class PluginManager;

PluginRef drumsRef();

class DrumMachine : public juce::AudioPluginInstance
{
public:
    explicit DrumMachine (PluginManager&);
    ~DrumMachine() override;

    // Message thread ------------------------------------------------------------
    const drumkit::Kit& kit() const { return current; }
    // The kit as given: samples and plug-ins that changed are loaded, the rest kept.
    // A pad whose sample or plug-in can't be loaded keeps it, silent; `problems` says why.
    void setKit (drumkit::Kit, juce::StringArray* problems = nullptr);
    juce::AudioPluginInstance* padPlugin (int pad) const;
    bool padSounds (int pad) const;           // its sample or plug-in is loaded
    void audition (int pad, int velocity = 110);
    PluginManager& pluginManager() { return plugins; }

    std::atomic<int> playingStep { -1 };      // the pattern's step now, for the window; -1 stopped
    std::function<void()> onKitChanged;      // its window's

    // Set by the app. An edit in its window as one undo step; without it, done at once.
    static std::function<void (DrumMachine&, const juce::String& label, std::function<void()> change)> edit;
    // The musician's scanned sounds, for Random Samples.
    static std::function<const library::Index*()> sounds;
    // Says something in the message line.
    static std::function<void (const juce::String&, bool warning)> say;
    void perform (const juce::String& label, std::function<void()> change);

    //==========================================================================
    const juce::String getName() const override { return "Kalibre Drums"; }
    void fillInPluginDescription (juce::PluginDescription&) const override;
    void prepareToPlay (double, int) override;
    void releaseResources() override;
    void reset() override;
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

private:
    using Sample = std::shared_ptr<const juce::AudioBuffer<float>>;
    struct Pad
    {
        Sample sample;                                     // at the device's rate
        std::string samplePath;                            // what `sample` was read from
        std::unique_ptr<juce::AudioPluginInstance> plugin;
        PluginRef pluginRef;
        // The audio thread's.
        double pos = -1;                                   // the sample voice; < 0 silent
        float gain = 0;
        int offIn = -1;                                    // samples until the plug-in's note-off
        int heldNote = -1;
        juce::MidiBuffer midi;
    };
    struct Trigger { int at = 0, pad = 0, velocity = 0, gate = 0; bool off = false; };

    PluginManager& plugins;
    drumkit::Kit current;                                  // read by the audio thread under `lock`
    std::array<Pad, drumkit::maxPads> pads;
    juce::CriticalSection lock;                            // the audio thread only tries it
    std::map<std::string, Sample> sampleCache;             // message thread
    std::array<Trigger, 1024> triggers;
    int triggerCount = 0;
    juce::AudioBuffer<float> scratch;
    std::atomic<int> auditionPad { -1 }, auditionVelocity { 0 };
    double rate = 48000;
    int block = 512;

    Sample loadSample (const std::string& path, juce::String& error);
    void addTrigger (const Trigger&);
    void schedulePattern (int numSamples);
    void renderSamples (juce::AudioBuffer<float>&, int from, int to);
};

} // namespace kal
