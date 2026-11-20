# 0102. Random devices in the rack, a random scale, and plug-ins that can't load

## Context

Asked (20 November): that the Random button's pool include the plug-ins the
musician has scanned; a Random button in the device rack that adds devices
as the top bar's does; and a random scale in the Scale menu.

## Decision

- **The pool already was the scanned plug-ins** (0101): every one known to
  the Plug-in Manager and not turned off, but those that measure or route
  (analysers, meters, tuners...) and Kalibre's own, which are added as
  themselves (the Sampler with a sound). A test now says so: two instruments
  and an effect scanned are in the pool, an analyser isn't, a "Drum" category
  makes a drum instrument.
- **The rack's Random** (under + Add), `Controller::randomDevices`, through
  `randomsong::devices` in Core, the same choosing as the song's: an
  instrument when the track plays notes (a track, with MIDI clips or none)
  and has none, a drum one for a track named Drums, Beat or Perc when there
  is one; one or two effects, never the same twice; a MIDI effect a quarter
  of the time when there are notes to change. A return, folder or a track of
  audio clips gets effects only; the Cue track only effects (0051). **Not the
  master** (amended the same day, from a test that appeared in the checkout;
  the musician had no preference): effects by chance on the whole mix are too
  much of a gamble, so Random isn't shown there and adds nothing if asked.
  The top bar's Random leaves the master as it is too. One undo step.
- **A plug-in that can't be loaded is left out.** A scan can be older than
  what's on the disk: a plug-in moved or deleted since stays in the list.
  Both Random buttons take out any device they added that didn't load,
  within the same undo step (Undo still goes back to before the click), and
  the message line says how many.
- **Random Scale** in the Scale menu: any root and any of the scales, not the
  one it is; one undo step, said in the message line.

## Consequences

- Tested: the pool; the rack's Random (an instrument and 1-2 effects, only
  effects the second time, one undo step each, a return effects only); a
  plug-in that can't load left out over 30 clicks, each undone cleanly; the
  device picker in Core over 80 seeds (a kit for drums, the Sampler always
  with a sound, about a quarter with a MIDI effect); Random Scale.
- Found by the tests: tests that add plug-ins to the scanned list left them
  in this machine's list (saved between runs), so a later test chose a fake
  plug-in. The tests now take theirs out.
