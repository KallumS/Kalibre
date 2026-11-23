/*
    Notation - a clip's notes as a single-staff part, and back (decision 0020).

    This is notation for entering and reading notes, not for engraving a
    score: one staff (treble or bass, chosen by the notes' range, or a grand
    staff for a piano), notes that start together drawn as one chord, held
    notes broken into tied note values at the bar line, rests filling the gaps.
    Note names are spelled for the project's scale, as ScaleView spells them.

    Entering a note: choose a value (whole to 1/32, dotted, triplet), click a
    line or space, and the note goes in at that spot, spelled pitch from the
    scale (Option-click a sharp, Shift-click a flat).

    No JUCE in here.
*/

#pragma once

#include "Model.h"

#include <string>
#include <vector>

namespace kal::notation
{

enum class Clef { Treble, Bass };

struct Spelled
{
    int letter = 0;        // 0 C ... 6 B
    int accidental = 0;    // -2..2
    int octave = 4;        // scientific: middle C is C4
};

// A pitch spelled for the scale ("F#" in G major, "Gb" in Db major).
Spelled spell (int pitch, const ScaleChoice&);

// Steps above the clef's bottom line (0 = bottom line, 1 = the space above it ...).
int staffStep (const Spelled&, Clef);
// The pitch a staff step means in this scale, with an accidental added.
int pitchAtStep (int step, Clef, const ScaleChoice&, int accidentalNudge = 0);

Clef clefFor (const std::vector<Note>&);

enum class Value { Whole, Half, Quarter, Eighth, Sixteenth, ThirtySecond };

Tick ticksOf (Value, bool dotted = false, bool triplet = false);

struct Duration
{
    Value value = Value::Quarter;
    bool dotted = false;
    Tick ticks = PPQ;
};

// A length broken into note values, longest first, each a plain or dotted value.
std::vector<Duration> split (Tick length);

struct Glyph
{
    Tick start = 0;
    Duration duration;
    bool rest = false;
    std::vector<int> pitches;          // a chord's notes, low to high
    std::vector<Spelled> spelled;
    std::vector<int> steps;            // staff steps, matching pitches
    bool tieToNext = false;
};

struct Bar
{
    int index = 0;
    Tick start = 0, length = 0;
    int numerator = 4, denominator = 4;
    std::vector<Glyph> glyphs;
};

// Where a note's start or end is read on the staff: the nearest 1/32 or
// 1/16 triplet, so notes played or humanised a few ticks off the grid read
// as the notes they were (the UAT). The notes themselves aren't moved.
Tick shown (Tick);

// A clip's notes laid out bar by bar. Ticks are the clip's own.
std::vector<Bar> layout (const Clip&, const TempoMap&, const ScaleChoice&, Clef);

} // namespace kal::notation
