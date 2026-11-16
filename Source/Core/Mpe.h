/*
    Mpe - MIDI Polyphonic Expression (decision 0042).

    On a track set to MPE each note plays on a channel of its own (2 to 16;
    channel 1 is the zone's master), so it can bend, press and slide by
    itself: a Roli, Osmose, LinnStrument or Sensel played into it, or the
    note expression drawn in the piano roll's lane.

    A note's expression lives with the note (Note::expression): bends in
    semitones, pressure and slide (CC 74) from 0 to 1, at ticks from its start.

    No JUCE in here.
*/

#pragma once

#include "Model.h"

namespace kal::mpe
{

inline constexpr double bendRange = 48.0;   // semitones, MPE's default for member channels

// After recording from an MPE controller: the pitch bends, pressure and CC 74
// on each note's channel while it sounds become that note's expression.
void gatherExpression (MidiData&);

// The expression at a moment of a note (held from point to point).
NoteExpression expressionAt (const Note&, Tick sinceStart);

// 14-bit pitch bend for a bend in semitones.
int bendValue (double semitones);

} // namespace kal::mpe
