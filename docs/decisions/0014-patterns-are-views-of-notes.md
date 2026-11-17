# 0014. Drums, steps and Euclidean are views of the notes

## Context

Separate pattern data drifts from the notes it makes.

## Decision

The drum grid, step sequencer and Euclidean sequencer read the clip's notes into their grid and write the grid back as notes, one undo step per click. Nothing else is stored.

## Consequences

Euclidean settings aren't saved: the rhythm is, as notes.

Since 0059 each editor reads and writes only the notes it made, so they share one clip.
