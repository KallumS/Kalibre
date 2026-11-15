# 0008. A gesture is one undo step

## Context

Undo should take back what a person did, not each mouse move.

## Decision

Every change goes through `Controller::apply`, which keeps the project as it was. Views show a drag and ask once, on mouse up. Live drags (a fader, a velocity) keep their own snapshot and push it at the end.

## Consequences

Undo is by snapshot of the whole project (500 steps). Audio isn't in the project, so this stays small.
