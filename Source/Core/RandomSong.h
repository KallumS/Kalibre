/*
    RandomSong - the top bar's Random button (decision 0101): a whole song to
    start from, made by chance.

    A tempo and a key; eight bars, looped. Good Idea's engine writes the drums
    and a lead tune; Midi Suggester's engine puts chords and a bass under the
    tune, and sometimes a counter-melody over the chords (Ideas). Each MIDI
    part gets a random instrument (drums one that plays drums, when there is
    one); a loop or two may come from the musician's sounds, stretched to the
    tempo; effects are chosen by chance, and now and then a MIDI effect.

    Kept small, so it plays without overloading the Mac: 4 to `maxTracks`
    tracks, at most 2 effects a track and `maxEffects` in all, at most 2 MIDI
    effects. The same seed makes the same song.

    The plug-ins and sounds to choose from are given (the App knows them);
    a Kalibre Sampler's sample is said, for the App to load. No JUCE in here.
*/

#pragma once

#include "Model.h"

#include <string>
#include <vector>

namespace kal::randomsong
{

struct Choice
{
    std::string name;
    PluginRef ref;
    DeviceRole role = DeviceRole::Instrument;
    bool drums = false;          // an instrument that plays a drum kit
    bool wantsSample = false;    // Kalibre Sampler: given one of the musician's sounds to play
    bool kit = false;            // Kalibre Drums: chosen for the drums when there are drum hits for its pads
};

struct Sample
{
    std::string path, name;
    double seconds = 0, sampleRate = 48000;
    int channels = 2;
    double bpm = 0;              // known well enough to stretch (its name, its file, or a loop heard exactly)
    bool drums = false;          // by its name: kick, snare, hat, drum, beat, break, perc...
    int note = -1;               // the note it plays, when it has one
};

struct Catalogue
{
    std::vector<Choice> instruments, effects, midiEffects;
    std::vector<Sample> samples;
};

struct Options
{
    unsigned seed = 1;
    int maxTracks = 8;
    int maxEffects = 8;
    int maxMidiEffects = 2;
    // Tracks to add to a song there is (Shift-click Random, 0119): made in its key and at
    // its tempo, their ids counted on from its own, so they join it without clashing.
    bool intoSong = false;
    double bpm = 120;
    ScaleChoice scale;
    Id firstId = 1;
};

struct Result
{
    Project project;
    // Each Kalibre Sampler, and the sound to load into it.
    std::vector<std::pair<Id, std::string>> samplerSamples;
    // Each Kalibre Drums, to give its pads drum sounds of their kinds (the musician's answer to the UAT).
    std::vector<Id> drumKits;
    std::string summary;         // "6 tracks at 112 BPM in E Minor (Natural)"
    std::vector<std::string> parts;   // a line a track: "Bass: Kalibre Synth, Reverberator"
};

Result make (const Catalogue&, const Options&);

// The device rack's Random (0102): devices for one track, chosen as the song's
// are. An instrument when `instrument` (a drum kit first when `drums`); a MIDI
// effect a quarter of the time (only with notes to change); one or two audio
// effects. A Sampler's sound in `sample`.
struct DevicePick { Choice choice; std::string sample; };
std::vector<DevicePick> devices (const Catalogue&, unsigned seed, bool instrument, bool drums, bool takesNotes);

// A loop: a sound long enough to loop, its tempo known.
bool isLoop (const Sample&);

// A device's Random button (decision 0104): which of a plug-in's controls chance
// leaves alone. Learned from the names the open-source plug-ins give their
// controls (Vital, Surge XT, Dexed, OB-Xf, Odin 2, Helm, Airwindows, Calf, LSP
// and others, over 2,000 names) and the usual ones' manuals. By the control's
// name, in words ("MasterVol" and "Osc1Pitch" are two each), its unit, and the
// plug-in's name. Says why: "volume", "pitch", "silence" (a sound source's on
// switch), "not sound" (presets, monitoring, quality, MIDI, mute, bypass) or
// "unknown" (#001, Param 12, a macro or a modulation slot: wired to anything);
// empty when chance may change it.
std::string whyKept (const std::string& name, const std::string& unit = {}, const std::string& plugin = {});
inline bool keepsItsValue (const std::string& name, const std::string& unit = {}, const std::string& plugin = {})
{
    return ! whyKept (name, unit, plugin).empty();
}
// Why a plug-in has no Random at all: "a pitch tool" (Auto-Tune, Little
// AlterBoy, a pitch shifter, a harmoniser, a tuner) or "a utility" (gain, trim,
// meters, analysers), whose controls are all volume or pitch. Empty otherwise.
std::string whyNoRandom (const std::string& plugin);

} // namespace kal::randomsong
