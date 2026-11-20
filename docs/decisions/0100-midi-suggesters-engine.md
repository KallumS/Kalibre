# 0100. Midi Suggester's engine, for chords, a bass and melodies

## Context

The Random button (0101) was asked to use the family's engines for its MIDI:
Good Idea's, Miderator's or Midi Suggester's. Good Idea's was already in
Kalibre (0062): drum grooves and motifs. Midi Suggester (KallumS/Midi-Suggester)
suggests harmony for a melody and a melody for harmony; Miderator runs both
engines together. Its engine appeared in Kalibre's checkout during the work,
uncommitted and not made in this session; the musician said to use it and
commit it.

## Decision

- Midi Suggester's four engine files (`ms_theory`, `ms_read`, `ms_harmony`,
  `ms_melody`, from its `reascripts/`, commit 7514bb4) are copied unchanged
  into `Engines/midi-suggester/`, with its LICENSE, embedded as Good Idea's
  are, and listed in `Engines/engines.json` (a `lua-engine`, so
  `tools/update_engines.py` brings it up to date) and VENDORED.md.
- `Core/Ideas` runs it through the same Lua state as Good Idea, by a small
  bridge in the style of Miderator's adapter: `chordsFor` reads notes as a
  melody and returns its first suggestion's chords (voiced), their names, and
  a bass from their roots (a note a chord, below middle C); `melodyOver` reads
  notes as chords and returns a melody. Each reads the key from the notes, as
  Midi Suggester does, and a seed picks among its variations: the same seed,
  the same music.

## Consequences

- Tested: Good Idea's motif in A minor, chords and a bass under it covering
  its four bars, a melody over the chords, the same names from the same seed.
  (The test first set the key with root index 9, which is G flat in
  Kalibre's spelled list; A is 13. Corrected.)
- Not yet offered anywhere but the Random button; the pattern editors' Random
  could use it next.
