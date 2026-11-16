# 0030. Crossfades and fades

## Context

Audio clips that meet or overlap click without fades.

## Decision

Where two audio clips on a track overlap, the earlier fades out and the later fades in over the overlap, worked out when the track is compiled for playback, so moving a clip moves its crossfade. Every fade is equal-power (a quarter sine), so a crossfade keeps the level. Each audio clip has fade handles at its top corners to drag.

## Consequences

Fades are in the clip (`AudioData::fadeIn/fadeOut`); crossfades aren't stored, they follow the overlap.
