# 0064. Arm for audio (click, red) or MIDI (right-click, blue)

(Extended by 0065: Shift-click for both, Shift-right-click for MIDI input; `armMidi` became `armFor`.)

## Context

Asked on 16 November (night): the arm button, left-clicked, turns red and
arms the track for recording audio; right-clicked, turns blue and arms it
for recording MIDI. Armed for MIDI, it records the MIDI output of the
instruments and MIDI effects: a free-running arpeggiator's notes are
recorded into the track the way audio would be.

## Decision

- **Two ways to arm** (`Track::armMidi` beside `armed`, saved): a click arms
  for audio, a right-click for MIDI; the same click again disarms, the other
  switches. The Track menu has both (Arm for Recording Audio, Arm for
  Recording MIDI). A Summing track records sound only. Track groups (0037)
  carry the way a track is armed. Control surfaces' arm buttons keep the
  way it was last armed.
- **Colours** (`Theme.h`): armed for audio `armAudio` #FF8577, a red as every
  DAW has it; for MIDI `armMidi` #7DB4E6, a blue. Both light enough for the
  button's dark ink to read at 7:1 (7.6 and 8.2), as every word must. A
  button may now carry an "on" colour of its own; the accent stays for
  everything else that's on.
- **Armed for audio** records the sound coming in, and monitoring on Auto
  follows it, as arming did. It no longer records MIDI.
- **Armed for MIDI** records what the track's instrument is played: the
  engine taps the first instrument, in the wrapper round every plug-in
  (`TimedPlugin::tap`), and catches the MIDI going into it, the MIDI effects'
  output, arpeggiators included, and what comes out of it. If the
  instrument sent notes of its own (one with an arpeggiator inside), those
  are what's kept; otherwise what it was played. With no instrument, the
  last MIDI effect is tapped; with neither, what's played in is recorded, as
  before. The audio thread writes into a fixed queue on the track's Source
  (`tapped`), with each event's place in the song; the window turns them
  into a clip (or takes, going round a loop, 0029) as it does notes played
  in. It doesn't monitor or record the sound coming in.
- Recording with nothing armed arms the chosen track for MIDI if it has an
  instrument or MIDI effect or no sound coming in, else for audio.
- What the instrument is played includes the track's own clips, as audio
  resampled from it would: recorded over existing clips without a MIDI
  effect changing them, they come out twice.

## Consequences

Tested: click, right-click, again, the other, a Summing track, saved; a
track with a test arpeggiator (a C5 every quarter of a second, whatever
comes in) and the synth, armed for MIDI and recorded through the live audio
path for two seconds: one MIDI clip of the arpeggiator's notes a quarter of
a second apart; armed for audio, no MIDI clip. In the running app: Track 1
clicked red, Track 2 right-clicked blue, then clicked red.
Found on the way: a button's click is acted on when the mouse comes up, when
it no longer says which button it was; the arm button keeps it from the press.
