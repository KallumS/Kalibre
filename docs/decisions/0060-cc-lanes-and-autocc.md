# 0060. CC lanes, and AutoCC's Auto button

## Context

Asked on 16 November (evening): MIDI CC lanes in the piano roll and in
notation, with the controllers every DAW lists (the user pointed to
professionalcomposers.com/midi-cc-list, which couldn't be reached from the
build machine; its list is MIDI 1.0's standard names, used here). And
AutoCC, the user's REAPER JSFX (KallumS/AutoCC), built in: an **Auto**
button, clickable only when the track names one of AutoCC's instruments,
that fills the CC lanes as AutoCC would; a violin track gets AutoCC's
violin (strings) curves.

## Decision

- **CC lanes** (`App/CcLanes.*`): under the piano roll's velocity lane, and
  under the notation's staff, one lane a controller, each named with its
  number and standard name (`Core/Controllers.*`). Drag to draw (values hold
  until the next point, as CC does), right-drag to clear; one undo step a
  drawing. Click a lane's name: Clear Lane, Remove Lane, Show Lanes. The
  toolbar's **CC Lanes…** shows or hides any of the 128, the usual ones
  first. The lanes are the track's (`Track::ccLanes`, saved); every track
  starts with CC1 and CC11, and any CC a clip already uses is shown too. In
  notation each value sits under the note it goes with, since notation's
  spacing follows the notes, not time.
- **Auto** (in the toolbar beside CC Lanes…, for the piano roll and
  notation): AutoCC's engine, ported from `AutoCC.jsfx` step for step
  (`Core/AutoCc.*`): every 5 ms, each of its four fixed lanes rises, settles,
  sustains while the key is held and falls; Legato (a note within 0.4 s of
  the keys coming up carries the line on), the sustain pedal holding the
  arc, velocity scaling 10% of CC1's swell, a value written only when its
  whole number changes. Its presets as they are: CC1 dynamics, CC11
  expression, CC7 volume (held at 94), CC21 vibrato (held at 92), fitted to
  Lorne Balfe's mock-ups. Pressing it plays the chosen clip's notes through
  that engine and writes the result into those four lanes, replacing what
  they had (nothing else), as one undo step, and shows the lanes.
- **Which instrument**: AutoCC's presets are by family, so the track's name
  says which, else its instrument plug-in's name: violins, violas, celli,
  basses → Strings; horns, trumpets, trombones, tuba → Brass; flutes, oboes,
  clarinets, bassoons, saxophones → Woodwinds; orchestra, tutti → AutoCC's
  Default (every family together). Two-word names first: an English horn is
  a woodwind, a bass trombone brass. AutoCC's own looser words (bass,
  legato, long) are left out, so "Synth Bass" isn't strings. With no family,
  Auto is greyed and its tip says to name the track for its instrument.
- AutoCC's other settings (Mode, Trigger, Depth, Time, Dynamic) are at its
  defaults; not yet shown in Kalibre.

## Consequences

Tested in the core against AutoCC's own README: a held string note goes
85 → 116 on CC1 (118 on CC11), holds, and falls back within a second; a key
touched for 200 ms takes Strings CC1 only to 91; a note 0.2 s after the last
carries the line on, after 2.5 s it swells again from 85; brass starts at
102 and blooms to 109, woodwinds 46 to 63; Auto replaces only its four
lanes. The names: Violins 1, Vln II, Celli, Double Basses, Horns a4, Bass
Trombone, Tpt 1, English Horn, Bass Clarinet, Flute, Full Orchestra; not
Synth Bass, Piano or Track 1. In the app: Auto does nothing but explain on
"Piano", fills four lanes on "Violins 1", undoes and redoes as one step;
renders 19 and 20.
