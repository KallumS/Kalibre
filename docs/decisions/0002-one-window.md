# 0002. Everything in one window

## Context

Floating plug-in windows and dialogs pile up and hide each other.

## Decision

One window: the transport along the top, the tracks in the middle, and the lower zone for devices, editors and the mixer. Plug-in windows are shown in the lower zone, side by side. The browser and the audio settings are a panel on the left. Small questions (a tempo, a name) are asked over the window, inside it (`ui::present`). The Mac's own open and save panels are the only windows of their own.

## Consequences

A plug-in that won't scale is shown at its own size and the rack scrolls. The core of the app (no JUCE) never knows about windows.
