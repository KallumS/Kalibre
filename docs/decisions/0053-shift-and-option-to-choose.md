# 0053. Shift-click chooses a range, Option-click adds

## Context

The user asked for Shift range selection wherever it makes sense, and for
Option-click (Alt) to add to what's chosen, on tracks, buttons and the rest
(16 November).

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
- **Shift-Option-click**: takes this one out of what's chosen.
- **Command-click**: adds or takes away, as before.
- **Option-click on a track's buttons** (mute, solo, arm, fold, its version,
  the mixer's polarity, Mono and 1 to 4) adds the track to the selection and
  doesn't press the button; Shift-Option-click takes it out.
- **Notes in the piano roll** (asked for, 16 November): the same. Shift-click
  a range of notes (the pitches and the time between the two), Option-click
  adds, Shift-Option-click takes out. Shift-drag still chooses an area and
  Shift-Option-drag takes one out; Option-drag still copies.
- Option-*drag* on a clip still copies it.
- The Plug-in Manager's list: Shift and Command as above (the list's own),
  Option-click adds, Shift-Option-click takes out.

## Consequences

- The piano roll's rules (0009) changed with the user's yes.
- Sliders (faders, pan) aren't changed by Option.
