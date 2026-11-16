# 0053. Shift-click chooses a range, Option-click adds

## Context

The user asked for Shift range selection wherever it makes sense, and for
Option-click (Alt) to add to what's chosen, on tracks, buttons and the rest
(17 November).

## Decision

One rule for every place things are chosen, kept in the Controller
(`chooseTrack`, `chooseClip`):

- **Click**: only this one.
- **Shift-click**: everything from the last one clicked without Shift to
  this one. Tracks run in the order the view shows them (the arrangement's
  rows, or the mixer's strips, the master and Cue track included); the last
  clicked is shared, so a track clicked in the arrangement can be the start
  of a range in the mixer. Clips: every clip on the tracks between the two,
  from the earlier start to the later end.
- **Option-click**: adds this one to what's chosen.
- **Command-click**: adds or takes away, as before.
- **Option-click on a track's buttons** (mute, solo, arm, fold, its version,
  the mixer's polarity, Mono and 1 to 4) adds the track to the selection and
  doesn't press the button.
- Option-*drag* on a clip still copies it.
- The Plug-in Manager's list: Shift and Command as above (the list's own),
  Option-click adds.

## Consequences

- The piano roll is left as it is: its mouse rules are the user's (0009),
  and there Shift-click toggles a note, Shift-drag chooses an area and
  Option-drag copies. Asked whether to change it.
- Sliders (faders, pan) aren't changed by Option.
