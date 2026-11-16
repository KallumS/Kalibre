# 0063. Swing, gate and repeat

## Context

Asked on 16 November (night): swing, gate and repeat in the step sequencer
and the drum grid; swing and gate in the Euclidean sequencer.

## Decision

- **Swing and gate are the pattern's**, one pair for each editor on each
  clip (`Clip::drumsFeel`, `stepsFeel`, `euclidFeel`; saved). Swing pushes
  every second step late by up to 75% of a step; gate is how long each note
  sounds, 5-100% of its step (the drum grid and Euclidean start at 90%, the
  step sequencer at 50%, as they did). Two small sliders, each named with its
  value, in each editor's top bar; a drag is one undo step. In the step
  sequencer the gate sets every step's (each step still keeps its own, read
  from its note, if it's changed in the piano roll); new steps take it.
- **Repeat is each step's**: 1 to 8 times in the step's length (a flam, a
  roll, a ratchet), each repeat gated. It joins the menu of what a drag sets
  (Velocity ▾, 0061), in the drum grid and step sequencer only (the piano
  roll's notes are the repeats themselves). A repeated cell is drawn cut into
  as many; a tied run in the step sequencer plays once.
- **Read back from the notes, still** (0014): the first note of a repeated
  step says how many (`Note::repeat`); its repeats say 0, "a repeat of the
  one before", and are skipped when the editor reads its notes. Swung hits
  are matched to the step whose swung start is nearest, so a hit pushed past
  half a step stays its own.

## Consequences

Tested: a hit repeated three times is three gated notes in its step, read
back as one hit ×3; hits swung 70% read back onto their own steps; a
repeated, swung, gated step written and read back; each editor's swing and
gate saved; in the app, dragging the drum grid's swing to 50% moves the
off-steps half a step, the gate's drag and the swing's are an undo step
each, and the step sequencer's gate sets every step. In the running app:
swing at 38% on a Good Idea reggaeton groove, a snare at ×4.
