# 0062. Random, from Good Idea

## Context

Asked on 16 November (night): a Random button in the drum grid, the step
sequencer and the Euclidean sequencer that picks new notes every click. The
user pointed to Good Idea, Miderator and Midi Suggester. Good Idea makes
musical ideas from the rules of music, each one a number (its 0002: chance,
but reproducible), with drum grooves as a kind of idea (its 0013) and every
setting left to chance (its 0005). Miderator runs Good Idea's engine, and its
sisters', as they are through an embedded Lua (its 0003).

## Decision

- **Good Idea's own engine**, not a new set of dice: `gi_theory.lua` and
  `gi_idea.lua` copied unchanged into `Engines/good-idea/` (from
  KallumS/Good-Idea at 91500c5, `Engines/VENDORED.md`), embedded in the app
  by `cmake/EmbedLua.cmake` (Miderator's), and run by `Core/Ideas.*` through
  the Lua 5.4 Kalibre already carries for scripting, in a state of its own.
- **Drums → Random**: a Good Idea Drums idea (a backbeat, half-time, four on
  the floor or breakbeat; hats or ride; pairs of bars answering each other;
  fills) as many bars as the grid shows (1 to 16), snapped onto its steps.
  A drum the kit's rows haven't got gets a row with its General MIDI name
  (the pedal hi-hat, a cowbell).
- **Steps → Random**: a Good Idea Motif (a short hook, one cell repeated and
  varied) in the song's key, as many bars as the steps show (1 to 4), one
  note a step; a note longer than a step is tied across.
- **Euclidean → Random**: new steps, hits and rotation for every drum (not
  Good Idea's: its Euclidean kicks are inside its grooves), in the ranges
  each drum usually plays: a kick 2-4 hits in 8, a snare 1-2 turned onto the
  backbeat more often than not, a hat 3-7.
- **Every click a new idea number** (1 to 99,999), named in the message line
  as Good Idea names them ("Good Idea 69632 - Drums (breakbeat), 1 bar") with
  what it rolled; the same number gives the same idea. Each is one undo step
  and replaces only that editor's notes (0059): the piano roll's stay.
- Midi Suggester suggests a melody or harmony for notes already written; a
  fit for a later "suggest" button, not for Random.
- Found on the way: the chord readout and chord lane read drum-grid hits as
  harmony ("D#maj7sus4/D"). Notes the drum grid or the Euclidean made are
  drums, and are left out now.

## Consequences

Tested: Good Idea's drums from Kalibre (General MIDI only, a kick on every
downbeat, as long as asked, more than 20 grooves in 30 numbers, the same
number the same groove) and its motifs (in G major but for a passing note,
as long as asked, more than 25 tunes in 30); the Euclidean ranges; the
three buttons in the app (a new pattern each click, one undo step each, the
piano roll's note kept, steps on the steps); drums left out of the chords.
In the running app: idea 69632, a breakbeat on the ride with a snare fill;
motif 21142 in C major; new rhythms for kick, snare and hat.
