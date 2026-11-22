# 0126. The Multiband device

## Context

Asked (22 November): a multiband device, like the Container, showing the
frequency spectrum split into bands (2 to 4, chosen), plug-ins in each band,
the bands dragged along the spectrum to grow or shrink them, never
overlapping.

## Decision

- **Add > Kalibre > Multiband**: 2, 3 or 4 bands (buttons along its top);
  three crossover parameters (30 Hz to 18 kHz), each kept at least a third of
  an octave (x1.25) from its neighbours, both when dragged and when
  automated. Up to four plug-ins on each band, one after another; each band
  On, Solo and Volume. Bands hidden by fewer keep their plug-ins.
- **The split**: Linkwitz-Riley crossovers, 24 dB an octave, below and above
  at each crossover in turn, each band below turned by an all-pass at every
  crossover above its own; the bands add back up to every frequency's level
  (the phase turns near the crossovers). A first try cut each band from the
  rest (rest less its low-pass): exact, but a band leaked 12 dB into the next,
  and the tests caught it.
- **Untouched when empty**: with no plug-in, every band on, at 0 dB and none
  soloed, the sound isn't split at all (0081's cancellation holds exactly).
- **Delay**: a band's plug-ins' delay holds the others back to meet it; the
  device's delay is the longest band's. Summed at 64 bits (0081).
- **The picture**: the input's spectrum (2048-point FFT, read from a ring the
  audio thread writes), the bands shaded, a handle on each crossover to drag.
- **Plug-ins' windows** in windows of their own, as an X/Y Morph corner's
  (0114), retired when closed (0115). The plug-ins and their states kept in
  the device's own state.

## Consequences

Tested: untouched exactly when empty; across four split bands every
frequency from 60 Hz to 12 kHz within 0.1 dB; a silenced low band takes the
low away and leaves the high; crossovers dragged past one another stop short;
a band's 100-sample delay brings the whole sound 100 samples later and
nothing else changed; saved and opened again. Render 47.
