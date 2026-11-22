# 0114. X/Y Morph's corners in windows of their own

## Context

Asked (22 November): on the Mac, a plug-in added to an X/Y Morph corner
should open in a window of its own, not inside the device's panel beside the
pad; clicking A, B, C or D opens that corner's plug-in's window. Inside the
panel it was a plug-in's native view inside a device that is itself shown
through a `PluginClip` (0058): cramped, and the panel had to be wide.

## Decision

- **Each corner's plug-in in a window of its own** ("X/Y Morph  A: Serum"),
  owned by the X/Y Morph (`MorphProcessor::showCornerWindow`), placed a step
  apart for each corner. It opens when a plug-in is loaded from the device's
  menu (not when a song opens), and when the corner's letter is clicked; the
  letter is lit while its window is open. Right-click a letter for its menu
  (Load Plug-in…, Load Sample…, Show Its Window, Clear); an empty or sample
  corner shows it on a click too, and its letter keeps the ellipsis.
- **Nothing inside the panel** but the pad and the four letters: it's 360
  points wide, not 640.
- **The window goes before its plug-in** (0056): a new plug-in or a sample in
  that corner, Clear, the X/Y Morph removed from the song (`Engine::bury`
  closes them at once), or deleted. A question (`ui::present`) hides them
  meanwhile, as other plug-ins' own windows.
- The same on Linux, where the tests run.

## Consequences

Tested: a corner loaded with a song opens no window; the panel holds no
plug-in's window; clicking A opens the plug-in's own editor in its own window,
lit; closing it tells the plug-in first; loading another plug-in or Clear
closes it; removing the X/Y Morph closes its corners' windows at once.
0002 and 0018 amended.
