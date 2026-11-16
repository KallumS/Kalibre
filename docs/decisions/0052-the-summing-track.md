# 0052. The Summing track

## Context

The user wants to record the whole mix back onto a track (resampling): a
track that, armed and recording, takes the master's output from where
recording starts until it stops (17 November).

## Decision

- Track > Add Summing Track. It's an ordinary track in every other way
  (clips, takes, versions, plug-ins), but takes no input.
- Armed, pressing Record records the master's output onto it from where
  recording starts until Stop: a take at that point, like any recording.
  Not armed, it records nothing.
- A hidden recorder hears the master's fader (before the Cue track, 0051,
  and without the metronome), so it can't feed back on itself. While it
  records, the Summing track's own clips don't play into the master.
- The master arrives after the graph's delay compensation (0016): the
  moments before recording began are trimmed from the take, so it starts
  exactly where recording started and lines up to the sample (tested with a
  plug-in delaying 300 samples).

## Consequences

It records the master's first two channels: in a surround mix, the front pair.
