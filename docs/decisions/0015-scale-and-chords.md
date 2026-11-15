# 0015. The scale and the chord at the top

## Context

Musicians want to know what they're playing, and to stay in key.

## Decision

The project's scale is chosen at the top (ScaleView's 18 roots and 16 scales, `ScaleModel.h` unchanged) and shades its rows in the piano roll, the root's darker. The chord sounding at the playhead (clips plus keys held) is named big at the top with ScaleView's chord reader, and the chord lane along the timeline uses Miderator's detector. Find the Scale from the Notes uses Midi Suggester's key ranking.

## Consequences

Tracks named drums, percussion or kit, and notes on channel 10, are left out.
