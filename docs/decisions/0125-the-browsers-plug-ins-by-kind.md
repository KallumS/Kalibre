# 0125. The browser's plug-ins by kind, and right-click

## Context

Asked (22 November): in the browser, plug-ins separated into MIDI effects,
instruments and effects; right-clicking one should add it to a new track or
show it in the Finder. Then: the other tabs should have right-click options
like the Sounds tab's.

## Decision

- **Plug-ins**: three headings, MIDI Effects, Instruments, Audio Effects (as
  the plug-in is filed: what the scan learned or the musician chose, 0069),
  each with how many, each A to Z; a heading isn't dragged or chosen.
  Right-click a plug-in: **Add to a New Track** (named for it, after the
  chosen track, one undo step, `Controller::addPluginOnNewTrack`), **Add to
  the Chosen Track** (as a double-click), **Show in Finder** (its bundle; an
  Audio Unit's looked for by name where they live; greyed when not found).
- **Presets**: right-click a preset: Load, Load on a New Track, Show in
  Finder, Delete… (asked first).

## Consequences

Tested: the headings, their order, a heading not dragged, a plug-in on a new
track as one undo step. Show in Finder isn't tested (it opens the Finder).
