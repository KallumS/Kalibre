/*
    Harmony - the scale the project is in, and the chords it plays.

    The scale is chosen by the musician (View > Scale, or the Scale button at
    the top of the window) and shades its notes' rows in the piano roll. The
    chord shown at the top of the window is whatever sounds at the playhead -
    the clips' notes and the keys held on a MIDI keyboard - named the way
    ScaleView names it (`scaleview::chordName`, unchanged, decision 0015).

    The chord lane along the timeline is read with Miderator's detector,
    ported: beat by beat, merged where the harmony hasn't moved. Key finding
    is Midi Suggester's, ported (Krumhansl-Kessler profiles with the family's
    weights).

    Drums aren't harmony: notes on MIDI channel 10, and tracks whose name
    says drums or percussion, are left out.

    No JUCE in here.
*/

#pragma once

#include "Model.h"

#include <string>
#include <vector>

namespace kal::harmony
{

struct TimedPitch
{
    int pitch;
    Tick start, length;
};

struct ChordSpan
{
    Tick start = 0, end = 0;
    std::string name;
    std::vector<int> pitches;
};

struct KeyGuess
{
    int root = 0;      // scaleview root index
    int scale = 0;     // scaleview scale index: 0 Major, 1 Minor (Natural)
    double score = 0;
};

bool isDrumTrack (const Track&);

// Every pitched note in the project in [from, to), at its place in time,
// looped clips unrolled. Muted tracks and clips are left out.
std::vector<TimedPitch> collect (const Project&, Tick from, Tick to);

// The notes sounding at one tick.
std::vector<int> soundingAt (const Project&, Tick);

// A chord's name for pitches held together, spelled for the project's
// scale ("Fmaj7", "C/E"); empty for fewer than two pitch classes.
std::string chordName (const std::vector<int>& pitches, const ScaleChoice&);

// The chord lane: chords along [from, to), beat by beat.
std::vector<ChordSpan> chordsAlong (const Project&, Tick from, Tick to);

// The keys that fit the notes best, best first.
std::vector<KeyGuess> rankKeys (const std::vector<TimedPitch>&);

// Whether a pitch is one of the scale's notes.
bool inScale (const ScaleChoice&, int pitch);
// Whether a pitch is the scale's root.
bool isRoot (const ScaleChoice&, int pitch);
// "C Major", "F# Dorian".
std::string scaleName (const ScaleChoice&);
// A note's name spelled for the scale ("Eb4").
std::string noteName (const ScaleChoice&, int pitch);

} // namespace kal::harmony
