# 0082. Each articulation's delay, and each note's

## Context

Asked (18 November): per-note and per-articulation delays for imported
articulations. Imported maps (0072) came in with every delay at 0, set only
from Track > Articulation Maps on the song's copy, so the next song using
the library's map started again at 0. Note Position Fixer couldn't be read
this time (access was refused), so "per note" is taken as each note's own
delay, on top of its articulation's.

## Decision

- **The Articulation Manager** has a Delay (ms) field under the
  articulations: choose one or several, type a delay (negative: earlier),
  press Return. It is kept in the library's map, for every song after, and
  in this song's copy of the same name (one undo step). The list shows each
  delay beside what the articulation sends.
- **Each note's own delay**: the piano roll's Velocity menu has Note Delay,
  a lane drawn up (later) or down (earlier) from the middle, ±250 ms, set as
  velocity is, the chosen notes together. It adds to the track's delay
  (0011) and the articulation's (0013). Saved with the song.

## Consequences

Tested: the compiled note moves by all three delays; the delay is kept in
the file; the manager sets the library's and the song's, one undo step;
render 24 shows two delays, render 26 the Note Delay lane.
