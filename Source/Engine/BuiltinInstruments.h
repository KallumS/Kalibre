/*
    BuiltinInstruments - Kalibre's own synth and sampler (decision 0039).

    Synth: two oscillators (saw, square, triangle, sine) and noise, a
    resonant low-pass filter with its own envelope, an amp envelope, glide,
    16 voices. The mod wheel adds vibrato; pitch bend is two semitones.

    Sampler: one sample across the keyboard, played from its root note, with
    start, end and a loop, an envelope and a filter, 32 voices. Drop a file
    on it from the browser or the Finder, or click Load Sample.

    Both are ordinary plug-ins to the rest of Kalibre: their knobs are
    parameters (automation, modulators, last touched, MIDI learn all work),
    their settings are saved with the project, and their windows sit in the
    lower zone like any other.
*/

#pragma once

#include "Model.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace kal
{

PluginRef synthRef();
PluginRef samplerRef();
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
class KalibreSampler : public BuiltinInstrument
{
public:
    KalibreSampler();
    const juce::String getName() const override { return "Kalibre Sampler"; }
    void fillInPluginDescription (juce::PluginDescription&) const override;
    void prepareToPlay (double, int) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 4.0; }
    juce::AudioProcessorEditor* createEditor() override;

    enum P { Root, Tune, Start, End, Loop, LoopStart, Cutoff, Resonance, VelSense, A, D, S, R, Volume, Count };

    // Message thread.
    bool load (const juce::File&, juce::String& error);
    juce::File file() const { return sampleFile; }
    const juce::AudioBuffer<float>& sample() const { return data; }
    std::function<void()> onLoaded;

private:
    struct Voice
    {
        int note = -1;
        float velocity = 0;
        double pos = 0, step = 1;
        juce::ADSR env;
        float ic1 = 0, ic2 = 0, ic1r = 0, ic2r = 0;
    };
    std::array<Voice, 32> voices;
    juce::AudioBuffer<float> data;
    double fileRate = 48000, rate = 48000;
    juce::File sampleFile;
    juce::SpinLock swapLock;
    float bend = 0;

    void writeExtra (juce::XmlElement&) override;
    void readExtra (const juce::XmlElement&) override;
};

} // namespace kal
