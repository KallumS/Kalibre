# 0013. Articulation maps, with a delay each

## Context

Orchestral libraries switch articulations with keyswitches, and each articulation speaks at a different time.

## Decision

A project holds articulation maps; a track uses one. Each articulation has a keyswitch, controller, program change or channel, and its own delay. A note's articulation is chosen in the piano roll's articulation lane. When compiled, the switch is sent a millisecond before the note, and the note moves by the track's delay plus the articulation's.

## Consequences

Maps are edited from Track > Articulation Maps.
