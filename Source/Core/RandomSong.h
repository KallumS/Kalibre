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
};

struct Result
{
    Project project;
    // Each Kalibre Sampler, and the sound to load into it.
    std::vector<std::pair<Id, std::string>> samplerSamples;
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

} // namespace kal::randomsong
