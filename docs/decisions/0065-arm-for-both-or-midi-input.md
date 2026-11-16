# 0065. Shift to arm for both, or for MIDI input

## Context

Asked on 16 November (night), after 0064: Shift-click arms a track for
recording audio and MIDI both; Shift-right-click arms it for the MIDI coming
in rather than from its plug-ins.

## Decision

- **Four ways to arm** (`Track::armFor`: Audio, Midi, Both, MidiInput, saved;
  it replaces 0064's `armMidi`, a few hours old, so a project saved with it
  comes back armed for audio):
  - Click: audio (red), the sound coming in.
  - Right-click: MIDI (blue), what the instrument is played, from the plug-ins (0064).
  - Shift-click: both (red and blue, half each): the sound coming in, and
    what the instrument is played, each its own clip.
  - Shift-right-click: MIDI input (blue, "In"): the notes as they come in
    from the keyboard, before any plug-in; an arpeggiator's notes aren't kept.
  The same way again disarms; another way switches to it. The Track menu has
  all four. A Summing track records sound only. Shift on a track's buttons
  was free: Option chooses tracks, Shift-Option takes them out (0053).
- Monitoring on Auto, and the sound coming in, follow arming for audio or
  both; MIDI from a keyboard reaches any armed track.
- A button's "on" colour may now be two: the second fills its right half.

## Consequences

Tested: the four ways, again to disarm, another to switch, a Summing track,
saved; through the live audio path with the test arpeggiator and the synth:
armed for both, an audio clip and a MIDI clip; armed for MIDI input, a key
played (E4) is recorded and the arpeggiator's C5s aren't, with no sound
recorded. In the running app: four tracks, one each way.
