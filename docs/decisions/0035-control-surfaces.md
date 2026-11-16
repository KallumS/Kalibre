# 0035. Control surfaces: Mackie Control and OSC

## Context

Faders under the fingers, and phones and tablets as remotes.

## Decision

Mackie Control, the protocol most surfaces speak, with banks of eight strips: motorised faders (one undo step from touch to release), V-Pots for pan, Rec, Solo, Mute and Select, the transport, the jog wheel, the display (names and levels), the timecode display (bars and beats) and meters. OSC for TouchOSC, Lemur, Open Stage Control and scripts, with Kalibre answering on a reply port. The protocol is `Core/Mackie` (tested without JUCE); `App/Surfaces` turns it into the controller's commands. The surface's port is taken out before the tracks hear it. Addresses and buttons are in docs/CONTROL.md.

## Consequences

- HUI (the older Pro Tools protocol) isn't spoken: most HUI surfaces also have an MCU mode.
- Not tried with a real surface yet; tested by feeding the protocol in and reading what comes back.
