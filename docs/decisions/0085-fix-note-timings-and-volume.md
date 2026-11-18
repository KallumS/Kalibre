# 0085. Fix Note Timings and Volume: Note Position & Volume Fixer, built in

## Context

Asked (18 November, late): bring the user's Note Position & Volume Fixer
(KallumS/Note-Position-Volume-Fixer, its `claude/kind-cray-otnrrc` branch)
into Kalibre. It exists for sloppily edited orchestral libraries: each
sample starts with the bow or breath before the note, so the attack is heard
30-150 ms late, by a different amount on every key and velocity; and some
notes of an articulation come out louder or quieter than their neighbours.
NPVF listens to a render of the part (a web page, REAPER scripts and a Mac
app share one engine), finds where each note is really heard and how loud,
and moves each note and changes its velocity or CC11. The user asked for it
as: right-click a MIDI clip, Fix Note Timings and Volume; the track frozen
and rendered silently, compared with the notes, unfrozen, the render
deleted, and the notes' starts and volumes fixed (perhaps with CC7).

## Decision

- **`Core/NoteFixer`**: NPVF's engine (`src/engine.js`) ported to C++, its
  decisions kept: the part's typical delay found first; each note measured on
  its own harmonics, band-passed forwards and backwards, after the last
  note's tail is projected and taken away (NPVF 0002); a note under held notes
  measured apart from their harmonics and marked; notes within 30 ms a chord,
  measured on the whole sound as one attack and moved together (NPVF 0003);
  the whole sound used when the harmonics don't hold the sound; the attack 12
  dB below the note's first peak (or the very start, 30 dB); loudness the
  loudest 50 ms, compared with a Huber-weighted fit of what velocity, CC1 and
  pitch predict. Added here: each **articulation** its own level in the fit,
  so notes are compared with the others of their articulation.
  Its tests are NPVF's own, ported with their made-up sloppy library and the
  same limits (Tests/TestNoteFixer.cpp).
- **Right-click a MIDI clip > Fix Note Timings and Volume…** asks how (kept
  for next time): Timing (each note's own delay, which keeps it on the grid;
  move the notes; leave it), Volume (CC7, CC11, velocity, leave it), and what
  to line up (the attack, or the very start of the sound).
- **The render**: the track as Freeze renders it (0080) but without unloading
  anything, only this clip, its effects bypassed so a reverb can't blur the
  attacks (their delay kept), into a temporary file, read and deleted. Freeze
  itself isn't needed: rendering one track already leaves every plug-in
  loaded, so there's nothing to unload or reload.
- **Timing** is measured from where each note was sent (with the track's, its
  articulation's and its own delay) and set so its sound lands where it's
  written. By default it becomes the note's own delay (Note Delay, 0082): the
  note stays written on the grid, quantising and notation are untouched, and
  fixing again finds nothing to change. Move the Notes moves them instead,
  their lengths kept, the clip starting earlier if one moves before it (a
  looped clip's notes are given delays instead); fixing again would move them
  again, as in NPVF.
- **Volume**: CC7 (or CC11) one value a note, a moment before it's really
  sent, from the level it was heard at (100 where none was set, the General
  MIDI default), on the General MIDI curve (40 log10); a chord's notes share
  one. A note sounding under or over another (legato) would move that one
  too, so it is changed by velocity, as NPVF does; so is a note sent before
  the clip begins. Velocity steps are the fitted dB per step (0.25 without).
- Notes played at random (chance, random timing or velocity range, 0061) are
  left alone. Every pass of a looped clip is measured; each note takes the
  middle of what its passes found. One undo step; the message line says how
  late the notes were and what was changed.

## Consequences

Tested in the app with a test instrument that is NPVF's sloppy library: each
note's delay within 3 ms (median) of the library's own numbers; the loud keys
turned down and the quiet one up by CC7; a second fix changes nothing; with a
track delay, the note delays make up the rest; moving the notes puts their
sounds on the grid; velocity mode; one undo step; the render deleted. Render 27
is the question.

Not carried over: NPVF's learned profiles (a library measured once, key by
key) and the REAPER and Mac measuring; Each Key and Whole Part timing; the
strength and tolerance settings (Kalibre uses NPVF's defaults: full strength,
1 dB). CC7 at 100 for notes that need no change lowers a part whose player
started at 127 by about 4 dB overall.
