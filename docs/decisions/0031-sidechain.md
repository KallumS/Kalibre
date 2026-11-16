# 0031. Sidechain routing

## Context

Compressors duck under a kick, gates open with another track: a plug-in listens to one sound while processing another.

## Decision

A plug-in with a second input has it switched on (stereo if it can, otherwise mono). Its menu has Sidechain From, listing every track that can feed it without the sound coming back round. That track's sound, after its fader, goes into the sidechain; it still goes to its own output too. Plug-in delay compensation lines the sidechain up like any other path.

## Consequences

- Tested: a click through a 200-sample plug-in arrives in the other track's sidechain at the same moment as the dry click.
- CLAP plug-ins' sidechains aren't wired yet (Kalibre's CLAP host gives them their main input only).
