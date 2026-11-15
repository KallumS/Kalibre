# 0026. Offline effects on clips

## Context

Some processing belongs to one clip, not a whole track.

## Decision

An audio clip can carry a chain of plug-ins (right-click > Offline Effects). Render processes the clip's audio through them into a new file the clip then plays; the original is kept and Back to the Original returns to it.

## Consequences

The chain's settings are its default; editing them in a window is next.
