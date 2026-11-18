# 0070. The message line tells what's chosen; the top bar chosen by right-click

## Context

Asked (17 November): the message line ("Deleted the track") a little
smaller, without its Dismiss button, and telling what's chosen, or what the
pointer is over when nothing is: a note's velocity, length and pitch, a
clip's length and number of notes. Also: right-click the top bar to choose
what it shows (to hide Tap, Punch, Count-in); a grid for the arrangement
there (1/1 to 1/64), as the piano roll has; Shift-click Follow to turn the
page when the playhead reaches the edge (as Miderator does), click to scroll
as now; and the CPU reading updating less often.

## Decision

- **The message line is always there**, a row 24 points high, in the small
  size. A message (what just happened, with Undo where it can be undone)
  stays until the choice changes or the pointer comes to rest on a clip or
  note; a warning (red-free, as every word) stays until the choice changes or
  it's clicked. Then the line tells, in the secondary colour, what's chosen
  (notes: pitch, velocity, length and where; several: how many, the range of
  each; clips: name, length, notes, where), else what the pointer is over.
  Lengths are said as a musician says them ("1/8", "2 bars 1 beat",
  `TempoMap::describeLength`). 0025 is amended: no Dismiss.
- **Right-click anywhere on the top bar** (its buttons and readouts too: a
  right-click there opens the menu instead of pressing them): a tick for each
  of Loop, Punch, Click, Count-in, Position, Tempo, Tap, Time Signature,
  Grid, Scale, Chord, Follow, CPU and Delay, Show Everything, and how often
  the CPU reading changes. Kept in Kalibre's settings. Play, Stop and Record
  always show.
- **Grid**, on the top bar, is the arrangement's own (`arrangeGrid`): 1/1 to
  1/64, triplets, snap on or off. Clips, the cursor, the loop and points in
  the arrangement snap to it; its lines are drawn as finely as the zoom lets
  them be seen. The editors keep their own grid in the lower zone (which
  gains 1/64). It starts at 1/16, as the shared one was.
- **Follow**: click to scroll with the playhead (or stop following);
  Shift-click to turn pages: the view stays still until the playhead reaches
  the right edge, then shows the next screenful from it. The button says
  "Follow: Page" then.
- **CPU** is measured as often as before and shown as the highest of the
  last two seconds (or one, or five), so a peak isn't missed and the figure
  doesn't flicker.

## Consequences

Tested: the line's message giving way (and a warning staying while only the
pointer moves), the words for a note and a clip, items hidden, the two
grids, Follow's two ways. Rendered. The line takes 24 points always, where
it took 32 only with a message.
