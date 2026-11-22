# 0119. Random replaces; Shift-click Random adds

## Context

Asked (22 November): the Random buttons in the device rack and the top bar
should clear everything first on a click, and add to what's there on a
Shift-click.

## Decision

- **The rack's Random**: a click takes the chosen track's devices out and
  puts devices by chance in their place (an instrument if the track plays
  notes), one undo step; Shift-click adds to them, as it did before.
- **The top bar's Random**: a click is a whole new song, as before (it asks to
  save first). Shift-click keeps the song and adds a random song's tracks
  after its own, made in its key and at its tempo, one undo step, with no
  question (nothing is lost). Their ids are counted on from the song's own
  (`randomsong::Options::intoSong`, `firstId`), so nothing clashes.

## Consequences

Tested: a click replaces a track's devices (none of the old ones left, an
instrument again), Undo brings them back, Shift-click keeps them; Shift-click
on the top bar's Random adds tracks, keeps the song's tempo, key and tracks,
every id its own, one undo step.
