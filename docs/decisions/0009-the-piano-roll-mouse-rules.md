# 0009. The piano roll's mouse rules

## Context

The user asked for these exactly.

## Decision

Left click in empty space adds a note (drag to lengthen it). Right click on a note removes it. Left-drag a note's body moves it (with every chosen note); left-drag its edge stretches or shortens it. Right-drag over an area removes everything in it. Shift-drag chooses an area; Shift-click toggles one note. Option-drag copies; Command drags off the grid. They live in `Core/Roll.*` as a state machine, tested without a window.

Changed by the user, 16 November 2026 (0053): choosing notes is as
everywhere else. Shift-click chooses every note from the last one clicked to
this one (the pitches and the time between them); Option-click adds a note;
Shift-Option-click takes one out; Shift-Option-drag takes an area out.
Shift-drag still chooses an area, and Option-drag still copies.

Changed by the user, 16 November 2026: a new note is as long as the last
note added, clicked or stretched (the grid's length until there's one), so
after drawing a 1/8 note the next click makes a 1/8; click a 1/16 note and
the next is a 1/16.

## Consequences

There's no pencil or eraser tool: the buttons are the tools.
