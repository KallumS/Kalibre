# 0023. The window is drawn on every build

## Context

The user works on a Mac; the code is often changed from Linux.

## Decision

`tools/RenderWindow.cpp` opens a demo song and draws the real window into PNGs, one per view, uploaded by CI as 'window-renders'.

## Consequences

Plug-in windows can't be drawn there; Kalibre's own can.
