# 0081. The audio engine's document: 64-bit mix points, neutral operations

## Context

Asked (18 November, with Kalibre_Audio_Engine_Architecture.docx): 32-bit
float processing everywhere, but 64-bit double precision wherever signals
meet at a summing point; and the rest of the document: which operations
must leave the sound untouched (neutral) and how that is proved by
cancellation tests, and how the non-neutral ones behave. Every version must
pass every test, and tests aren't changed to make Kalibre pass.

The last session had made Kalibre's own *files* 32-bit float (0079); the
processing was already 32-bit float. What wasn't as the document asks,
each found by a test written first (46 failures in the first run):

- Mix points summed in 32-bit float (JUCE's graph and Kalibre's plan, 0078),
  so eight pairs of 24-bit signals and their inverses didn't cancel.
- Exports to 16 or 24 bits went through JUCE's conversion, which truncates:
  even a 16-bit file rendered to 16 bits came back a step off on half its
  samples.
- The pan law's middle was 0.99999994, not 1: nothing passed unchanged.
- Every render started with the faders fading in from where they were (a
  fader set to -100 dB let the first 20 ms through).
- Two halves of a split clip both played the sample at the cut.
- Looped audio clips didn't loop (the second pass was silence or the rest of
  the file).
- A clip at its own tempo still went through the stretcher in every warp mode.
- A bypassed plug-in was asked to bypass itself: JUCE's default passes the
  sound without its latency, so it came out early (two clicks, not one).
- Volume automation was read once a block and smoothed.

## Decision

- **64-bit summing.** Wherever two or more signals meet on a channel in the
  plan (a track's input, folders, returns, the master, sidechains), they are
  added in double precision and the sum rounded to float once. A track's
  clips are mixed the same way (with what comes in live), and so is the
  Container's band with what passes it by. Processing stays 32-bit float.
- **The plan plays on one thread too.** With Use Every Core off the plan
  still plays (no workers), so its mix points are 64-bit; JUCE's own graph
  only plays the block after a change (live, not in a render) and, for the
  multi-core test, as a reference.
- **Exports**: each sample rounded to the nearest step of the file's word
  length (half away from zero), clipped to the word's range. Same or more
  bits than the source: identical. Dither only below 32 bits, never twice
  (freezes, bounces and Kalibre's own files are float, undithered).
- **Pan law**: constant power, sine curves; exactly 0 dB in the middle,
  +3 dB fully to one side, nothing on the other (`PanLaw.h`). The
  Container's pan too (it was a balance).
- **Faders and sends start a render at their level**, not faded to it.
- **Clips**: each sample of the timeline belongs to at most one clip, the one
  whose edges, each rounded to the nearest sample, hold it; a split meets on
  one sample. Looped audio clips play their content again each pass.
- **Unstretched warps**: a warped clip whose file runs at the song's speed
  throughout (its tempo, or its markers, agreeing with the song's tempo,
  checked at every marker, tempo change and between) is copied, never
  stretched, in every warp mode; the render is made again from the file
  whenever the tempo changes, so it is neutral again when the tempo returns.
- **Bypass removes the plug-in from the signal flow**: Kalibre's wrapper
  never calls it, ignores its own bypass, and passes what came in, MIDI too,
  delayed by its latency so delay compensation still lines up. Deactivating
  (0010) still unloads it.
- **Volume automation is followed sample by sample**: the track's Source
  works out the lane's level for every sample of the block (between ticks
  too) and the fader uses it.
- **Consolidate** (audio clips): the chosen clips on one track, as they play
  (gain, fades, crossfades, warping, loops; no plug-ins), mixed at 64 bits
  into a new 32-bit float WAV at the song's rate, normalised to 0 dBFS; the
  new clip's gain makes up for it. One undo step. MIDI clips as before.
- **Recording** is always 32-bit float (0079): at least the converter's bit
  depth, so always neutral; there's no lower setting to choose.
- **Tests** (`Tests/App/TestNeutral.cpp`, and in TestTrackKinds and
  TestContainer): white noise, sines and sweeps at 16, 24 and 32 bits
  rendered to 16, 24 and 32; playback placed and trimmed; every warp mode
  at its own tempo, and after the tempo changes and comes back; eight pairs
  and their inverses into the master, a folder and a return; routing through
  folders and pre-fader sends; the pan law; every Kalibre effect bypassed,
  and a bypassed latency kept; splits, warped or not, looped or not; volume
  automation per sample; a freeze; consolidating; the Summing track's
  recording of the master and a recording of the interface's input, each
  sample for sample.

## Consequences

The multi-core test (0078) compared Use Every Core off with on, assuming
off meant JUCE's graph. It now compares three: JUCE's graph (as reference,
within 1e-5, its sums being 32-bit), the plan on one thread and on every
core (identical, sample for sample).

Signals mixed across several summing points can still differ in the last
bit, as the document says. A plug-in that changes its latency while
bypassed beyond what was allowed for when it was prepared is held back by
that much at most. Pan automation is followed sample by sample too (since
18 November, late), and a switched-off plug-in keeps its delay (0086).
