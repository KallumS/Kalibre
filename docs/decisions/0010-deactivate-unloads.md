# 0010. Deactivating a plug-in unloads it and keeps its state

## Context

Big instruments take memory and processor even when muted.

## Decision

The power switch on a plug-in saves its state into the project and removes it from the audio graph, freeing its memory and processor. Switching it on makes it again and restores the state.

## Consequences

Loading takes as long as opening the plug-in does.

Bypass (0081) is the other way: the plug-in stays loaded and is taken out of the signal flow, its latency kept.
