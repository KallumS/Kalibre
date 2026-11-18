/*
    NoteSelect - choosing notes by what they are (decision 0075): the roots,
    thirds, fifths and the rest of the chords, the notes on the beat or off
    it, the bottom or top line. And Explode Chords: each voice of a clip's
    chords to a track of its own.

    A chord is the notes that start together (within a 64th, as played in).
    Its root is the pitch class the others stack in thirds from best (a third
    above it, then a fifth, then a seventh; the bass on a tie); each note is
    then named by how far above the root it is: a 2nd is a 9th, a 4th an
    11th, a 6th a 13th, as chords are named.

    No JUCE in here.
*/

#pragma once

#include "Model.h"
#include "TempoMap.h"

#include <optional>
#include <set>
#include <vector>

namespace kal::noteselect
{

enum class What { Root, Third, Fifth, Seventh, Ninth, Eleventh, Thirteenth, OnBeat, OffBeat, Bottom, Top };

// The notes of the clip that are `what`; `only` limits them to one editor's (0059).
std::set<Id> choose (const Clip&, const TempoMap&, What, std::optional<NoteFrom> only = std::nullopt);

// The chords: groups of notes (indices into clip.midi.notes) starting together.
std::vector<std::vector<size_t>> chords (const MidiData&, Tick together = PPQ / 16);

// The root's pitch class (0-11) of pitches held together, or -1 for fewer than two classes.
int rootOf (const std::vector<int>& pitches);

// What a pitch is in a chord with that root: 1 root, 3, 5, 7, 9, 11, 13 (0 for none of them).
int degreeOf (int pitch, int rootPc, const std::vector<int>& pitchesInChord);

// Explode Chords (0075): the clip's notes, voice by voice (bottom voice first),
// each to a new track made like the clip's own (its plug-ins and settings)
// just after it; the clip itself removed. Returns the new tracks, top voice
// first as they're listed. Nothing happens for a clip with no chords.
std::vector<Id> explode (Project&, Id clip);

} // namespace kal::noteselect
