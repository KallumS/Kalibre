# 0079. Kalibre's own audio is 32-bit float WAV

## Context

Asked (18 November): all bounced audio, and any audio used inside Kalibre,
as 32-bit floating-point WAV; exports still in whatever format the user
picks. Recordings and captured audio were written as 24-bit WAV, offline
effects, sample edits and freezes as 32-bit (which JUCE's WAV writer makes
float unless told otherwise), and imported files were used as they came
(MP3, FLAC, AIFF, 16-bit WAV).

## Decision

- `Engine/AudioFiles`: `floatWav` makes every writer Kalibre uses for
  itself, 32-bit with the floating-point format asked for by name.
  Recordings (and the Summing track's), captured audio, offline effects,
  sample-editor edits, freezes and bounces all go through it.
- **Audio brought in** (File > Import, dragged in, a DAWproject's audio)
  that isn't a 32-bit float WAV already is copied into the song's Audio
  folder as one, at its own sample rate; the clip uses the copy and keeps
  the original's name. One that is, is used where it is.
- **Exports** are untouched: WAV, AIFF, FLAC or MP3, 16, 24 or 32 bits, with
  dither, as before.

## Consequences

Nothing inside Kalibre is rounded, and a level over 0 dB is kept rather
than clipped (tested: 1.5 written, 1.5 read back). Imported files take more
room: a 16-bit file's copy is twice its size, an MP3's many times. Tested:
a 16-bit WAV imported comes in as a 32-bit float copy; a float one is used
where it is; freezes and bounces are float.
