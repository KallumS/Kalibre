# 0090. Fix Note Timings and Volume hears the samples, not the clip's dynamics

## Context

The user (18 November, late): the clip's velocities and CCs could mislead
the volume detection (0085), as a dip written with CC7, a CC1 swell or a
soft velocity sounds like a sample that's too quiet. They suggested setting
every velocity to 80 and clearing the CCs before the render, putting them
back after, or, cleaner, the fixer ignoring them altogether.

## Decision

The cleaner way. The quiet render is already made from a copy of the song,
so the copy is changed and the clip never is: there's nothing to put back.

- **Volume** is measured from a render with every note of the clip at
  velocity 80 and its controllers and pressure left out; CC1 80, CC7 100
  and CC11 127 are set at its start, so an instrument that remembers its last
  values plays every note alike. A difference left is the sample's own.
- **Timing** is measured from a render as written: a library's delay depends
  on how hard a note is played, so the velocities are kept for it.
- With one of the two off, only the other render is made. The dB a
  velocity step is worth comes from the render as written.

## Consequences

Tested: a note played quieter with CC7 50 keeps CC7 50 (it used to be turned
back up to about 100); velocities stay as they were; the faulty keys are
still found. Two renders take twice as long as one.
