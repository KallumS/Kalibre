# 0101. The Random button: a whole song by chance; a new song lets go of the last one's plug-ins

## Context

Asked (20 November): a random button at the top right that makes a whole
project ready to go: a random number of tracks, a random instrument on each,
random effects, a chance of a MIDI effect, MIDI from Good Idea's, Miderator's
or Midi Suggester's engine, or audio from the scanned sample folders. Kept
small (under 10 or so tracks and effects) so the Mac isn't overloaded.

## Decision

- **Random**, at the very top right of the top bar (first row, however many
  rows), and **File > New Random Song**. It makes a **new song**, as File >
  New does, after asking to save a changed one (`MainComponent::checkSaved`):
  replacing the song keeps it simple to roll again, and nothing is mixed into
  the musician's work.
- **The song is made in Core** (`Core/RandomSong`, no JUCE, a seed): a tempo
  (82-148 BPM, or a loop's own half the time when there are loops), a key
  (mostly major or natural minor, sometimes Dorian or Mixolydian), eight bars
  looped. Good Idea writes the drums and a four-bar tune (repeated); Midi
  Suggester puts chords and a bass under the tune and a counter-melody over
  the chords (0100). Drums, bass, chords and lead always; then by chance a
  counter-melody, a pad (the chords an octave up, softer) and up to two loops,
  to **4-8 tracks**.
- **Instruments by chance**, from the scanned instruments and Kalibre's own
  that sound by themselves (Kalibre Synth, Trombolese, Reese Basser), and
  Kalibre Sampler given one of the musician's sounds (only when there is one).
  Drums go to an instrument whose name or category says drums, else the
  Sampler with a drum hit, else any.
- **Effects by chance**: 0-2 a track, at most 8 in all, from the scanned
  effects and Reverberator; a MIDI effect a quarter of the time, at most 2,
  from the scanned ones and Midi Evolver. Plug-ins that measure or route
  (analysers, meters, tuners...) aren't chosen.
- **Loops** from the Sounds tab's index (0094, 0095): only those whose tempo
  is known (named, in the file, or a loop heard exactly), a looped audio clip
  over the eight bars, warped to the song's tempo. Copied as 32-bit float WAV
  (0079).
- **The App** (`App/RandomSongs.cpp`) lists what there is, loads each
  Sampler's sound into its saved state, gives the tracks the house colours,
  and puts the song in place; the message line says what it made.
- **Found on the way: a new song kept the last one's plug-ins.** Devices are
  numbered from 1 in every song, and the engine keeps instances by
  (version, device) number and plug-in. Opening a song whose device 2 was the
  same plug-in as the last song's device 2 played it with the last song's
  settings. `Engine::newSong` lets go of every instance; Open, New, New from
  Template, Open a DAWproject and Random call it first. A test (two songs
  saved, each a Kalibre Synth as device 2, set differently) failed, then
  passed.

## Consequences

- Tested: 40 seeds, 4-8 tracks, at most 2 effects a track and 8 in all, at
  most 2 MIDI effects, drums on a kit, every part with notes, the Sampler only
  with a sound, the same song from the same seed; loops warped and looped;
  in the app, every instrument loaded, the Sampler's sound loaded, loops
  copied, four seconds exported and not silent; the button at the top right,
  asking to save first.
- What it sounds like depends on the plug-ins and sounds the musician has;
  with none scanned it's Kalibre's own instruments and Reverberator.
- Render 40 is a random song.
