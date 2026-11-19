# 0083. The Note Splitter

## Context

Asked (18 November): a device that splits a track's notes to the MIDI
inputs of other tracks: with four outputs, a four-note chord's top note goes
to output 1, the second note down to output 2, the third to 3 and the bottom
to 4. Kalibre had no MIDI routing between tracks.

## Decision

- **Add > Kalibre > Note Splitter**, a MIDI effect. Its panel: how many
  outputs (1 to 16, four to begin with), and the track each goes to, labelled
  Top note, 2nd note down, ..., Bottom note; New Tracks makes a track for
  every output without one, under this one. Each change is one undo step.
  Which track each output feeds is the song's (`Device::midiTo`).
- **The rule** (`Core/NoteSplit`, no JUCE): notes starting together are a
  chord, ranked top to bottom; with fewer outputs than notes the lower ones
  share the last; with fewer notes, they fill from the top. Notes played by
  hand within 30 ms of the chord's first count as the chord: one that joins
  can move one already started to its rightful output (ended on the old,
  started on the new). Notes held from before stay where they are; a note
  ends where it started; controllers, the pedal and bends go to every output;
  a key's own pressure follows its note. A stop or a jump ends every note.
- **The engine** gives each output a node of its own, fed from the splitter's
  MIDI, into the target track's Source, which takes it as MIDI played in:
  heard through its instruments, recorded if armed for MIDI. The chord also
  goes on unchanged to the splitter's own track's instruments. A route that
  would feed back into its own splitter is left out. The output nodes read
  what the splitter routed in the block, after it (they hang off it), so
  nothing is shared while both play (0078); bypassed, the outputs end their
  notes and send nothing.

## Consequences

Tested: the rule (a chord in any order, fewer outputs, fewer notes, a chord
played bottom first, a held note, controllers, a stop); a song played live
with four armed tracks, each recording its note of the chord; the panel's
outputs, New Tracks and Undo. Render 25. Not yet tried on the Mac with real
instruments.
