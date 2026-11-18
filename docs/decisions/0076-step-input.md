# 0076. Step input

## Context

Asked (18 November): step input in the piano roll, notation and drum
grid. While it's on, hold keys on the MIDI keyboard, press a number on the
computer's keyboard for the length, let go, and the notes are written at
that length: 1 = 1/128, 2 = 1/64, 3 = 1/32, 4 = 1/16, 5 = 1/8, 6 = 1/4,
7 = 1/2, 8 = 1/1; 9 then a number for a dotted one (9, 3: a dotted 1/32),
0 then a number for a triplet.

## Decision

- **Step Input**, a toggle on the editor bar, shown in the piano roll,
  notation and drum grid (and turned off on leaving them).
- `Core/StepInput`: the keys held, every key held since the last chord, the
  length chosen. When the last key lets go, every key held since is written,
  together. The length stays chosen until another number; 9 and 0 apply to
  the next number only. It starts at 1/16.
- The engine, while step input is on, tells the window of each key as it's
  played (still heard through the track). The numbers 0-9 (and the keypad's)
  go to step input before any other key while it's on.
- Written into the chosen clip at the cursor (a looped clip: into its
  loop), with the keys' velocities, as the drum grid's notes in the drum
  grid and the piano roll's elsewhere; the clip grows to the bar they end
  in; no clip chosen: a new one on the track, from the cursor's bar. One undo
  step a chord. The cursor moves on by the length; the new notes are chosen.
- The message line says how it works and the length now.

## Consequences

Tested: the lengths, dotted and triplet; a chord written on its last key's
release, at the cursor; drums; off, keys are only heard. No rests yet (move
the cursor, or Rest in notation).
