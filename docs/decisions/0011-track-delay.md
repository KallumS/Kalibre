# 0011. Track delay plays a track early or late

## Context

Slow-attack libraries need playing early to sound in time.

## Decision

A track's delay (milliseconds, negative for early) is applied when its clips are compiled into playback lists: every event moves by it, so a negative delay really is early, reading ahead on the timeline.

## Consequences

Live input can't be played early; the delay applies to what's on the timeline.
