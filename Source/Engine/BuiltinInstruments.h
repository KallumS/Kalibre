/*
    BuiltinInstruments - Kalibre's own synth and sampler (decision 0039).

    Synth: two oscillators (saw, square, triangle, sine) and noise, a
    resonant low-pass filter with its own envelope, an amp envelope, glide,
    16 voices. The mod wheel adds vibrato; pitch bend is two semitones.

    Sampler: one sample, played three ways (0095): Keys, across the keyboard
    from its root note; Slices, cut where its hits are (the sensitivity
    chosen), one a key from the root up; Granular, as many short grains, so its
    speed and its pitch are apart (Speed 0 holds it still). Start, end and a
    loop, Speed, an envelope and a filter, 32 voices. Drop a file on it from
    the browser or the Finder, click Load Sample, or Random Sample for any
    sound from the Sounds tab's folders (0108). Each is one undo step.

    Both are ordinary plug-ins to the rest of Kalibre: their knobs are
    parameters (automation, modulators, last touched, MIDI learn all work),
    their settings are saved with the project, and their windows sit in the
    lower zone like any other.
*/

#pragma once

#include "Model.h"
#include "AudioTools.h"
#include "Library.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace kal
{

PluginRef synthRef();
PluginRef samplerRef();
PluginRef tromboleseRef();   // decision 0093
std::unique_ptr<juce::AudioPluginInstance> makeInstrument (const PluginRef&);

// What both share: parameters by name, state as XML, a knob editor.
class BuiltinInstrument : public juce::AudioPluginInstance
{
public:
    BuiltinInstrument();
    struct Group { juce::String name; std::vector<juce::RangedAudioParameter*> params; };
    std::vector<Group> groups;   // how the editor lays out its knobs

    float value (int index) const { return params[(size_t) index]->convertFrom0to1 (params[(size_t) index]->getValue()); }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void releaseResources() override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

protected:
    std::vector<juce::RangedAudioParameter*> params;
    void addFloat (const juce::String& group, const juce::String& id, const juce::String& name, juce::NormalisableRange<float>, float def,
                   const juce::String& suffix = {});
    void addChoice (const juce::String& group, const juce::String& id, const juce::String& name, juce::StringArray, int def);
    virtual void writeExtra (juce::XmlElement&) {}
    virtual void readExtra (const juce::XmlElement&) {}
};

//==============================================================================
class KalibreSynth : public BuiltinInstrument
{
public:
    KalibreSynth();
    const juce::String getName() const override { return "Kalibre Synth"; }
    void fillInPluginDescription (juce::PluginDescription&) const override;
    void prepareToPlay (double, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override;   // every voice silent: an export starts clean (0097)
    double getTailLengthSeconds() const override { return 4.0; }
    juce::AudioProcessorEditor* createEditor() override;

    enum P { Wave1, Wave2, Detune, Fine, Mix, Noise, Cutoff, Resonance, EnvAmount, KeyTrack,
             FA, FD, FS, FR, AA, AD, AS, AR, Glide, Volume, Count };

private:
    struct Voice
    {
        int note = -1;
        float velocity = 0;
        double phase1 = 0, phase2 = 0, freq = 440, target = 440;
        juce::ADSR amp, filter;
        float ic1 = 0, ic2 = 0, ic1r = 0, ic2r = 0;   // the filter's state, left and right
        juce::uint32 age = 0;
        juce::Random noise;
    };
    std::array<Voice, 16> voices;
    double rate = 48000;
    juce::uint32 counter = 0;
    float bend = 0, wheel = 0;
    double vibrato = 0;
    int lastNote = -1;

    void noteOn (int note, float velocity);
    void noteOff (int note);
    void render (juce::AudioBuffer<float>&, int start, int n);
};

//==============================================================================
// Trombolese (decision 0093): KallumS/Trombolese's physical model, a trombone
// crossed with an oboe, played one note at a time. Each note finds its partial
// and slide position; a note played while one sounds slides to it. Bore morphs
// cylinder to cone (the mod wheel adds to it), Reed lips to double reed; pitch
// bend moves the slide two semitones.
class TromboleseInstrument : public BuiltinInstrument
{
public:
    TromboleseInstrument();
    ~TromboleseInstrument() override;
    const juce::String getName() const override { return "Trombolese"; }
    void fillInPluginDescription (juce::PluginDescription&) const override;
    void prepareToPlay (double, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 1.0; }
    void reset() override;
    juce::AudioProcessorEditor* createEditor() override;

    enum P { Bore, Reed, Breath, Attack, Release, Glide, Volume, Count };

private:
    std::unique_ptr<struct TromboleseVoice> voice;
    double rate = 48000;
    std::array<int, 16> held {};
    int heldCount = 0;
    int note = -1;
    float velocity = 0.8f, bend = 0, wheel = 0;
    double breath = 0, lip = 0, slide = 0, lipTarget = 0, slideTarget = 0, level = 1, levelTarget = 1;
    std::vector<double> block;
    void start (int note, float velocity, bool legato);
    void render (juce::AudioBuffer<float>&, int start, int n);
};

//==============================================================================
class KalibreSampler : public BuiltinInstrument
{
public:
    KalibreSampler();
    const juce::String getName() const override { return "Kalibre Sampler"; }
    void fillInPluginDescription (juce::PluginDescription&) const override;
    void prepareToPlay (double, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 4.0; }
    void reset() override;
    juce::AudioProcessorEditor* createEditor() override;

    enum P { Root, Tune, Start, End, Loop, LoopStart, Cutoff, Resonance, VelSense, A, D, S, R, Volume,
             Mode, Speed, Sensitivity, GrainSize, Density, Spray, Position, Count };
    enum class PlayMode { Keys, Slices, Granular };
    PlayMode mode() const { return (PlayMode) juce::jlimit (0, 2, juce::roundToInt (value (Mode))); }

    // Where the slices start at the sensitivity chosen, in samples of the
    // sample (the first is Start). Not for the audio thread: it allocates.
    std::vector<int> slicePoints();

    // Message thread.
    bool load (const juce::File&, juce::String& error);
    void unload();
    // Any sound from the scanned folders (0108), not the one it has; its Root
    // set to the note the scan heard, when it heard one. False, with why, when
    // none could be loaded. One undo step through `edit`.
    bool loadRandom (juce::String& said);
    juce::File file() const { return sampleFile; }
    const juce::AudioBuffer<float>& sample() const { return data; }
    std::function<void()> onLoaded;

    // Set by the app, as for Kalibre Drums: a change as one undo step (without
    // it, done at once); the musician's scanned sounds; the message line.
    static std::function<void (KalibreSampler&, const juce::String& label, std::function<void()> change)> edit;
    static std::function<const library::Index*()> sounds;
    static std::function<void (const juce::String&, bool warning)> say;
    void perform (const juce::String& label, std::function<void()> change);

private:
    struct Grain { double pos = 0, step = 1; int age = 0, length = 0; bool on = false; };
    struct Voice
    {
        int note = -1;
        float velocity = 0;
        double pos = 0, step = 1;
        double sliceEnd = -1;                  // Slices: where this one's slice ends
        double head = 0, untilGrain = 0;       // Granular: where it's reading, the next grain
        bool released = false;
        std::array<Grain, 24> grains;
        juce::Random random;
        juce::ADSR env;
        float ic1 = 0, ic2 = 0, ic1r = 0, ic2r = 0;
    };
    std::array<Voice, 32> voices;
    juce::AudioBuffer<float> data;
    std::vector<audiotools::HitCandidate> candidates;   // every hit at the highest sensitivity, made on load
    // The slice `index` at the sensitivity chosen: [from, to). False when there's no such slice. No allocation.
    bool sliceBounds (int index, double start, double end, double& from, double& to) const;
    double fileRate = 48000, rate = 48000;
    juce::File sampleFile;
    juce::SpinLock swapLock;
    float bend = 0;
    void seedGrains();

    void writeExtra (juce::XmlElement&) override;
    void readExtra (const juce::XmlElement&) override;
};

} // namespace kal
