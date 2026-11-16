# 0003. Audio and MIDI on the same track

## Context

Most DAWs make a track audio or MIDI. Musicians keep a part together: a recorded guitar and its MIDI doubling, a sample and its trigger.

## Decision

Every ordinary track holds both. A clip is MIDI or audio, and both kinds sit on the same lane. The track's source plays the MIDI into its MIDI effects and instruments and the audio clips straight into its audio effects, summed with the instruments.

## Consequences

There is no separate audio or instrument track type. An audio effect that wants MIDI (a vocoder) gets the track's notes.
