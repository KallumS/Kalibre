# 0054. Snap to zero crossings

## Context

A cut where the waveform isn't at zero clicks. The user asked for an option
to snap to zero crossings wherever audio is handled (16 November).

## Decision

- One option: Edit > Snap to Zero Crossings, and the Zero Crossings button in
  the sample editor (the same setting, lit when on). It's remembered between
  runs.
- With it on, the nearest point within 10 ms where the waveform (both
  channels mixed) crosses zero is used:
  - when an audio clip's start or end is dragged in the arrangement (after
    the grid snap, so it lands next to the grid line);
  - for Split at the Cursor, each audio clip at its own crossing;
  - for both ends of a range chosen in the sample editor.
- It's found in the audio as it plays: the file, or the warped render.
- A tick is about 25 samples at 120 bpm, too coarse to land on a crossing.
  So an audio clip's edges each have a fine part (`AudioData::startFine`,
  `endFine`): seconds past the tick, under half a tick either way. Playback
  starts and stops on that exact sample; a split gives both halves the same
  one. Trimming an edge sets its fine part afresh (zero with the option off).

## Consequences

- MIDI isn't affected. Comping between takes isn't snapped: comps have their
  own crossfades (0030). Neither are fades, which are made to be heard.
- Tested: a 70 Hz sine split with the option on is cut where it reads 0 and
  both halves meet there; off, the cut is on the cursor's tick.
