# 0127. Stereo Split

## Context

Asked (22 November): a device like the Multiband (0126) with a vectorscope
showing the middle, left and right frequencies (or the middle and the
sides, chosen), and plug-ins on each part: Left, Middle, Right, Sides.

## Decision

- **Add > Kalibre > Stereo Split**, two splits, buttons along its top:
  - **Left, Middle, Right**: each frequency, moment by moment, goes to the
    part its place between the speakers falls in, judged by how loud it is on
    each side (a short-time Fourier transform: 2048 points, Hann windows in
    and out, a quarter overlapping, so the parts add back up exactly). Two
    **edges** (parameters, 100% L to 100% R; 30% L and 30% R at first) are
    dragged on the vectorscope, kept at least a tenth apart, and each is soft
    over that tenth, so a sound moving across one fades from part to part
    rather than jumping. The window is the device's delay in this mode,
    whatever's in it: a plug-in switched in or out never moves the sound
    (compensated as any plug-in's, 0016).
  - **Middle and Sides**: (L+R)/2 in both sides, and (L-R)/2 on the left
    with its opposite on the right: two stereo parts that add up to the
    sound, no delay. A plug-in on Sides is given the sides as they are in the
    sound. **Middle's plug-ins are the same in both splits**; a part not in
    play keeps its plug-ins and closes their windows.
- **Each part** (as each Multiband band): up to four plug-ins, On, Solo,
  Volume; a part's plug-ins' delay holds the others back to meet it; summed
  at 64 bits (0081). The parts, plug-ins, windows and state are now one
  `SplitProcessor` shared with the Multiband, its saved songs read as before.
- **Untouched when empty**: nothing in it, every part on at 0 dB, none
  soloed: the input passes untouched (one window later in Left, Middle,
  Right), a cancellation test.
- **The vectorscope**: the middle straight up, left and right on the
  diagonals, louder further out (rings every 12 dB to -60): the last 1024
  samples as grey dots (out of phase reaches the sides), and each of 96
  frequencies (30 Hz to 16 kHz) as a dot at its place, coloured from low to
  high (the track colours, content not words); the parts shaded behind, the
  edges' handles to drag.
- The split mode is a parameter: changed by automation or Random it's taken
  up on the message thread (the delay changes with it).

## Consequences

Tested: untouched exactly when empty in both splits; with a plug-in at 0 dB
the parts add back up (within 1e-5, one window later; Middle and Sides
within 1e-6); a silenced Left takes a tone on the left alone and leaves the
middle's and right's; a silenced Middle takes the middle's alone; silenced
Sides make the sound mono; edges dragged past one another stop short; a
Left plug-in's 100 samples come out on the window plus 100; mode, edges and
plug-ins saved and opened again. Renders 48 and 49. Not yet heard on the Mac.
