# 0141. Join, and the arrangement's drags as in Ableton

## Context

Asked (26 November), testing Kalibre: a way to join several separate clips
on the timeline: choose them, right-click, **Join**. And: dragging over
several clips to choose them wasn't possible: a drag in empty space moved the
playhead. Dragging from the bottom half of a track should mark a range, as in
Ableton; from the top half, a box choosing clips, as Shift-drag does in the
piano roll. Asked whether that should apply on clips too, the musician chose
Ableton's way: a clip moves by its name bar, and a drag lower in it marks a
range.

## Decision

- **A clip's name bar** (the strip with its name, now a shade darker, a
  line under it) moves it. **Lower in a clip**, a drag marks a time range
  across the tracks dragged over (the Range tool's, 0092: Delete clears what's
  in it); a click there still chooses the clip. Its edges trim, Shift-drag
  resizes (0095), Option-drag copies from anywhere on it, fades and warp
  markers are dragged as before. The pointer says which: a hand over the
  bar, an I-beam lower down, the resize arrows at the edges.
- **Empty space in a track**: the top half drags a **box** that chooses
  every clip it touches, on every track it crosses (Shift or Command adds to
  what was chosen; drawn as the piano roll's box); the bottom half drags a
  **time range**. A click still puts the cursor there and chooses the track.
  A press waits until the mouse has moved 3 points before it's a drag.
- **Join** (first in a clip's menu, Edit > Join, ⌘J) replaces Consolidate
  the Chosen Clips, and does what it did on several tracks at once, as one
  undo step: each track's chosen clips become one clip there. MIDI clips are
  merged (`edit::consolidateClips`); audio clips that are pieces of one
  recording, meeting where they were cut, are put back together with no new
  file (`tools::glueAll`, the Glue tool's join, each to the next); other audio
  is rendered into a new file (as Consolidate, 0081), normalised, the clip's
  gain making up for it. A track with one chosen clip is left alone; a MIDI
  clip with an audio one, a locked clip or a frozen track's audio can't be
  joined, and the message line says which and why.
- The version is 1.4.0, something new. It was first made 2.0.0, as how clips
  are moved had changed; the musician chose 1.4.0: a changed gesture isn't a
  change to how Kalibre is used in the sense the first number keeps for.

## Consequences

- Moving a clip needs its name bar: in a short track that's a strip about
  16 points tall.
- The playhead no longer follows a drag in empty space; the ruler's upper
  half still scrubs.
- Tested: a click lower in a clip chooses it; a drag there marks a range
  across two tracks and moves nothing; a drag in a lane's lower half marks
  one, backwards too; the name bar moves the clip, one undo step; a click
  in empty space still puts the cursor there; a box across two tracks
  chooses the three clips it touches and not a fourth, Shift adds, a plain
  box starts again; Join of clips on two tracks makes one on each, notes
  where they played, one undo step, undone; one clip alone on each track
  changes nothing; `glueAll` puts a recording cut in three back as it was,
  leaves two recordings, two tracks or one clip alone. The tests that dragged
  a clip by its middle to move it now drag its name bar.
