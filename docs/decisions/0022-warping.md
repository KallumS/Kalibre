# 0022. Warping

## Context

Loops and recordings should follow the project's tempo.

## Decision

An audio clip can be warped: Repitch (like tape), Granular (50 ms grains; drums and textures) or Spectral (Signalsmith Stretch, MIT; tonal material, and transposing without changing length). The file maps to the timeline through warp markers, or the file's own tempo. A loop named with its tempo ('Drums 96bpm') is warped when imported. The warped audio is rendered on a background thread and played from memory.

## Consequences

Long files take a moment to be ready after a change.
