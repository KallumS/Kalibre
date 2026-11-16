# 0061. Note chance, random velocity range, random timing

## Context

Asked on 16 November (night): beside velocity, three more settings for each
note: the chance it plays (out of 100%), a range its velocity is chosen from
at random, and a range of milliseconds it may play early or late. Clicking
Velocity in the piano roll brings up a small menu of them; the same in the
step sequencer and the drum grid.

## Decision

- **Each note carries them** (`Note::chance`, `velocityRange`, `timingMs`,
  saved): 100%, 0 and 0 for a plain note. The drum grid and step sequencer
  keep them per step, through writing and reading their notes (0014).
- **Chosen afresh each time it plays**, so a looped clip comes out
  different every pass, and an export too. Compiling puts a note with a
  timing range early by that range, marked with the window it may land in
  (`MidiMessage::jitter`); the audio thread rolls the dice as each note-on
  comes up (`playback::roll`, a xorshift with no locks or allocation): it
  skips the note (and its note-off), moves its velocity within the range
  (never past 1-127), and holds a late one in a fixed list of 256 until its
  moment. A jump or a stop empties that list. A timing range is at most a
  quarter of the note's length, so a note never starts after it ends.
- **The menu**: the piano roll's lane name (Velocity ▾) lists Velocity, Note
  Chance, Random Velocity Range, Random Timing, then the controllers; the
  lane then shows and sets that, per note, as velocity is (the chosen notes
  together), with each value written beside its bar when it isn't a plain
  note's. The step sequencer and the drum grid have a Velocity ▾ button at
  their top left with the same four: in the drum grid, dragging a hit sets
  the chosen one; in the step sequencer, Option-drag does (and a plain drag,
  for any but velocity, since a plain drag there sets the pitch); each
  step's value is drawn as the bar inside it and, for the three new ones,
  in figures.
- Ranges: chance 0-100%, velocity ±0-64, timing ±0-50 ms.

## Consequences

Tested: compiling (the note-on early by its range, with its window, chance
and range; capped at a quarter of a short note); the dice (30% plays about
3,000 times in 10,000, 0% never, 100% always; velocities within ±20 and
1-127; delays within the window); the drum grid and step sequencer keeping
them through writing and reading; and an export through the sampler: 40
notes at 0% leave no click, at 50% between 8 and 32, at 100% with ±20 ms all
40, each within 20 ms of where it's written and most off the grid. In the
running app: the menu in all three, a step's chance dragged to 53% and shown
in the piano roll's Note Chance lane.
