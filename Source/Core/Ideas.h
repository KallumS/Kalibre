/*
    Ideas - Good Idea's engine, for the pattern editors' Random buttons
    (decision 0062).

    Good Idea (KallumS/Good-Idea) makes musical ideas from the rules of music,
    each one a number: the same number always gives the same idea, the next
    number a new one. Its engine is Lua, tested in its own repository, so it
    runs here as it is (Engines/good-idea/, embedded), through the Lua 5.4
    Kalibre carries for scripting, as Miderator runs it.

    No JUCE in here. One Lua state, made on first use: call from one thread.
*/

#pragma once

#include "Model.h"

#include <string>
#include <vector>

namespace kal::ideas
{

constexpr int maxSeed = 99999;   // Good Idea's idea numbers run 1 to this

struct Idea
{
    std::vector<Note> notes;   // from tick 0, in Kalibre's ticks
    Tick length = 0;
    std::string name;          // "Good Idea 63797 - Drums (backbeat), 2 bars"
    std::string summary;       // what it rolled: "Backbeat / hi-hats / Flowing ..."
    std::string error;         // the engine failed: a bug, said as such
};

// A drum groove, 1 to 16 bars, General MIDI drums (Good Idea's Drums).
Idea drums (int bars, int numerator, int denominator, int seed);

// A short tune in the key, 1 to 4 bars (Good Idea's Motif).
Idea motif (const ScaleChoice& key, int bars, int numerator, int denominator, int seed);

// Midi Suggester's engine (KallumS/Midi-Suggester, Engines/midi-suggester/, decision 0100),
// run as Miderator runs it: chords to go under a melody, with a bass line
// from their roots; and a melody to go over chords. Each reads the key from
// the notes, as Midi Suggester does. `length` is how long the music is.
struct Harmony
{
    std::vector<Note> chords, bass;
    std::string names;     // "Am  F  C  G"
    std::string error;
};
Harmony chordsFor (const std::vector<Note>& melody, Tick length, int numerator, int denominator, int seed);
Idea melodyOver (const std::vector<Note>& chords, Tick length, int numerator, int denominator, int seed);

} // namespace kal::ideas
