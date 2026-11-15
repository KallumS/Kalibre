# 0004. Tempo and time signature are automatable

## Context

A piece may speed up, slow down and change metre.

## Decision

Positions are ticks (960 a quarter). The tempo map is a list of points, each held or ramped linearly to the next; seconds are integrated exactly over a ramp. Time signature changes start on a bar line. Both are drawn and edited in the tempo lane (View > Tempo Lane) and set from the top bar.

## Consequences

A note stays on its beat when the tempo changes. Audio follows only when warped (0022).
