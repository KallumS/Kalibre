# 0059. One clip, every editor adds to it; the playing step lit

## Context

Asked on 16 November (evening): each way of entering MIDI (piano roll, drum
grid, step sequencer, Euclidean) needed an empty clip of its own, which felt
clunky. Notes drawn in one showed up in another as if they were its own, and
the step sequencer, written to, replaced every note in the clip. Asked: let
each add to the chosen clip alongside the others (or, failing that, make each
clip with one method). Also: show where the clip is playing, lighting the
step being played.

## Decision

- **Each note knows which editor made it** (`Note::from`: Roll, Drums,
  Steps, Euclid; saved with the project). The drum grid, the step sequencer
  and the Euclidean sequencer each read and replace only their own notes, so
  they all add to one clip and none takes over another's. The piano roll
  shows and edits every note, whoever made it; a note moved in the piano roll
  stays with the editor that made it. Played, recorded, imported and drawn
  notes are the piano roll's.
- Chosen over "one method per clip": nothing new to choose before starting,
  any clip takes any method, and the piano roll still sees the whole clip.
- **The playing step is lit**: the drum grid and the step sequencer light
  the column being played; each Euclidean circle rings the step being played,
  with a hand from the middle. Counted from where the patterns are written
  (the clip's offset), round its loop.
- Projects saved before this: their notes are the piano roll's, so a drum
  pattern made earlier shows in the piano roll and not on the drum grid.

## Consequences

Tested in the core: steps added to a clip with a piano roll note leave it
alone, the drum grid sees neither, writing again replaces only an editor's
own notes, and the tags survive saving. In the running app: a long C3 drawn
in the piano roll, the step sequencer then empty, four steps added, both in
the piano roll; step 5 lit while playing beat 2.
