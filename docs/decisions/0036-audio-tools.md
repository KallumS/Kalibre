# 0036. Audio tools and the sample editor

## Context

Audio needed the everyday tools: normalise, strip silence, find the hits, move them onto the grid, edit samples, and see the sound as a spectrum.

## Decision

The looking is in `Core/AudioTools` (no JUCE, tested): peak and normalise gain, silent stretches, transients from the rise of energy in 5 ms frames, warp markers that put each hit on the grid, sample edits on a range, an FFT and YIN pitch detection.

- **Normalise** sets the clip's gain; the file isn't touched.
- **Strip Silence** turns a clip into one clip a sound, each where it was, with 5 ms fades.
- **Quantise Audio** adds warp markers at the hits moved to the grid (0022's warping does the rest; Granular when the clip wasn't warped), pinning the clip's ends.
- **The sample editor** (Editor tab, when an audio clip is chosen): the whole file, the clip's part lit, the hits marked; drag to choose a range; Gain, Silence, Reverse, Fade In/Out, Invert, Remove DC. An edit writes a new file and points the clip at it, so the original is never changed and Undo goes back. Spectrum shows a spectrogram instead of the waveform.

## Consequences

- Each sample edit is a new file in the project's Audio folder.
- Clips with offline effects need rendering again after an edit (the message line says so).
