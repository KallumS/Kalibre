# 0075. Select notes by what they are; Explode Chords

## Context

Asked (18 November): a Select menu in the MIDI editors. Piano roll and
notation: the roots of the chords, their 3rds, 5ths, 7ths, 9ths, 11ths and
13ths (where present), every note on a beat, every one off it, the bottom
notes and the top notes. Step sequencer, drum grid and Euclidean: on the
beat and off it. And Explode Chords, from the piano roll, notation and the
clip's menu: each note of the chords to a clip on a new track of its own.

## Decision

- `Core/NoteSelect`. A chord is the notes starting within a 64th of each
  other (as played in). Its root is the pitch class the others stack best
  in thirds from: a third above scores 4, a perfect fifth 3 (diminished or
  augmented 1), a seventh 1, a minor second above -2, the bass 1 on a tie.
  So C E G A is C6, A C F is F over A. Each note is then named from the
  root as chords are: a 2nd a 9th, a 4th an 11th, a 6th a 13th; a minor
  third beside a major one is a #9, a tritone beside a fifth a #11, a minor
  sixth beside a fifth a b13; a diminished seventh is a 7th.
- On the beat: starts within a 64th of one of the meter's beats; off it:
  every other note. Bottom (top) notes: no other note lower (higher) is
  sounding as it starts: the bass line, the melody.
- **Select ▾** on the editor bar: In Each Chord (Roots, 3rds to 13ths), In
  Time (Every Beat, Every Off Beat), Along the Clip (Bottom, Top), Select
  All, Select None, and Explode Chords to Tracks. The pattern editors offer
  the beats and All/None, for their own notes (0059); the chosen ones are
  ringed in the drum grid and step sequencer. The message line says how
  many were chosen.
- **Explode Chords to Tracks** (Select ▾, and right-click clips): voices
  numbered from the bottom of each chord; a track for each, made like the
  clip's (its plug-ins and settings, no clips), named "Piano 1 (Bottom)" to
  "Piano 4 (Top)" and listed top voice first just after it, each with one
  clip holding its notes (offset and loop kept). The clip itself is
  removed: the notes moved. One undo step. A clip of single notes has
  nothing to explode.

## Consequences

Tested in the core (four chords, a melody over chords, explode) and through
the controller (one undo step). Spread chords wider than a 64th count as
separate notes.
