# 0071. The chord at the top is what's played; clips' chords when asked; loop to clips

## Context

Asked (17 November): the chord display at the top only for chords played in
on a MIDI keyboard; the chord lane above the tracks only for clips the user
asks for, with right-click > Detect Chords, on one clip or several chosen
with Shift-click and Option-click (0053). And right-click a clip to set the
loop to it (a 4-bar clip at bar 49: the loop from 49 for 4 bars), several
clips spanning them all.

## Decision

- The top bar's Chord names only the keys held on MIDI keyboards (the
  capture of what comes in); clicking it plays them.
- Each clip has `detectChords`, saved. The chord lane reads only clips that
  have it (`harmony::chordsAlong (..., detectedOnly)`). Right-click > Detect
  Chords turns it on for every chosen MIDI clip (the one right-clicked if it
  isn't chosen), and shows the chord lane; ticked when all have it, choosing
  it again turns it off. One undo step.
- Right-click > Set the Loop to the Clip(s): from the first chosen clip's
  start to the last one's end, the loop turned on. One undo step.
  (`song::loopToClips`.)
- Scripts and Find the Scale still read every clip.

## Consequences

Songs saved before show no chords in the lane until asked. Tested in the
core.
