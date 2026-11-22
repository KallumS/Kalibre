# 0123. The Sounds in six orders

## Context

Asked (22 November): beside A to Z, the Sounds tab should order by Z to A,
longest to shortest, shortest to longest, slowest to fastest and fastest to
slowest.

## Decision

The order menu has all six, and Shuffle. Length is a sample's own, a MIDI
file's beats at its tempo (or 120); tempo is what the scan knows (its name,
the file, or heard). Sounds whose tempo isn't known come last in the tempo
orders; the same length or tempo goes by name. Z to A turns the folders
round too. The button says the order shortly (A to Z, Longest, Fastest...)
to fit its quarter of the row; the choice is kept with the Sounds settings.

## Consequences

Tested in the core: four sounds in each of the six orders, the unknown
tempo last, the choice saved and read back.
