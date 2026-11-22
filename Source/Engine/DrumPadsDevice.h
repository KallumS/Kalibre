/*
    DrumPadsDevice - Drum Pads (decision 0113): 128 pads, one for each note, each
    a sample or a plug-in (a drum synth), with its own volume, pan, mute,
    solo and choke group; sixteen shown at a time in its window.

    Notes played to it (a clip's, a keyboard's) play the pad of that note.
    A pad's sample is read into memory at the device's rate when chosen (as
    Kalibre Drums reads them) and played as a one-shot, as loud as the hit; a
    pad's plug-in is hosted inside, sent the pad's plug-in note for each hit
    and its note-off when the note played in ends. A hit stops the other pads
    in its choke group: their samples fade out in 2 ms, so the cut doesn't
    click, and their plug-ins are sent their note-offs.

    The audio thread is handed the kit whole through `Swap`: it never locks
    or allocates, and an instance it played is let go on the message thread.
    The pads are added at 64 bits and rounded once (0081).

    The kit and each plug-in's settings are the device's saved state. Edits
    in its window go through `edit`, which the app makes one undo step each.
*/

#pragma once

#include "DrumPads.h"
#include "Swap.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>
#include <map>

namespace kal
{

class PluginManager;

PluginRef drumPadsRef();

class DrumPadsProcessor : public juce::AudioPluginInstance
{
public:
    explicit DrumPadsProcessor (PluginManager&);
    ~DrumPadsProcessor() override;

    // Message thread ------------------------------------------------------------
    const drumpads::Kit& kit() const { return current; }
    // The kit as given: samples and plug-ins that changed are loaded, the rest kept.
    // A pad whose sample or plug-in can't be loaded keeps it, silent; `problems` says why.
    void setKit (drumpads::Kit, juce::StringArray* problems = nullptr);
    // Which sixteen pads the window shows: kept with the song, not an undo step.
    void setView (int firstPad);
    juce::AudioPluginInstance* padPlugin (int pad) const;
    bool padSounds (int pad) const;           // its sample or plug-in is loaded
    void audition (int pad, int velocity = 110);
    bool takeHit (int pad);                   // played since last asked: the window lights it
    PluginManager& pluginManager() { return plugins; }
    int selected = 36;                        // the pad its window shows the controls of

    std::function<void()> onKitChanged;      // its window's

    // Set by the app. An edit in its window as one undo step; without it, done at once.
    static std::function<void (DrumPadsProcessor&, const juce::String& label, std::function<void()> change)> edit;
    // The musician's scanned sounds, for Random Samples.
    static std::function<const library::Index*()> sounds;
    // Says something in the message line.
    static std::function<void (const juce::String&, bool warning)> say;
    void perform (const juce::String& label, std::function<void()> change);

    //==========================================================================
    const juce::String getName() const override { return "Drum Pads"; }
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
    using Plugin = std::shared_ptr<juce::AudioPluginInstance>;
    static constexpr int pads = drumpads::padCount;

    // What the audio thread plays, made whole on the message thread.
    struct Data
    {
        std::array<Sample, pads> samples;      // null for a pad playing a plug-in
        std::array<Plugin, pads> plugins;
        std::array<double, pads> left {}, right {};   // volume and pan
        std::array<bool, pads> heard {};       // not muted, soloed if any is
        std::array<int, pads> pluginNote {};
        std::array<int, pads> choke {};
        std::vector<int> withPlugins;          // the pads playing plug-ins
    };
    // A pad's voice: the audio thread's own.
    struct Voice
    {
        const void* sound = nullptr;           // the sample it's playing, to know it changed
        const void* plugin = nullptr;
        int pos = -1;                          // < 0 silent
        double gain = 0;
        int fade = -1;                         // samples left of a choke's fade; -1 none
        int heldNote = -1, offIn = -1;         // the plug-in's held note; samples to its note-off
        juce::MidiBuffer midi;
    };
    struct Trigger { int at = 0, pad = 0, velocity = 0, gate = -1; bool off = false; };

    PluginManager& plugins;
    drumpads::Kit current;
    std::array<Sample, pads> samples;          // what's posted, kept here too
    std::array<Plugin, pads> instances;
    std::array<PluginRef, pads> instanceRefs;
    std::map<std::string, Sample> sampleCache;
    Swap<Data> data;
    const Data* playing = nullptr;             // the audio thread's last, to see a new one

    std::array<Voice, pads> voices;
    std::array<Trigger, 1024> triggers;
    int triggerCount = 0;
    std::array<std::atomic<bool>, pads> hits {};
    std::atomic<int> auditionPad { -1 }, auditionVelocity { 0 };
    juce::AudioBuffer<float> scratch;
    std::array<std::vector<double>, 2> sum;
    double rate = 48000;
    int block = 512, fadeLength = 96;

    Sample loadSample (const std::string& path, juce::String& error);
    void post();
    void addTrigger (const Trigger&);
    void render (const Data&, int from, int to);
    void choke (const Data&, int pad, int at);
};

} // namespace kal
