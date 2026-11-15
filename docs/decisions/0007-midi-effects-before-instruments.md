# 0007. MIDI effects first, then instruments, then audio effects

## Context

Scaler, arpeggiators and sequencers change notes before an instrument plays them.

## Decision

A track's devices are always ordered MIDI effects, instruments, audio effects (`edit::normaliseChain`), whatever order they're added or moved in. MIDI effects run in a line; every instrument is fed from the last of them, and they play side by side.

## Consequences

Moving a MIDI effect after an instrument isn't possible, by design.
