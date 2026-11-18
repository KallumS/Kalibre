# 0089. Every automation lane sample accurate

## Context

Asked (18 November, late): make all automation sample accurate. Volume and
pan were (0081); mute, send levels and plug-ins' parameters were read once a
block (512 samples at most), so a mute or a step in a filter landed up to a
block late.

## Decision

- The track's Source works out the song's tick at every sample of the block
  once (to a millionth of a tick, so a point exactly on a sample isn't missed
  by the last bit of the sums), and reads every lane there.
- **Volume, pan, mute and send levels** are applied sample by sample by the
  fader and the send.
- **Plug-ins' parameters**: plug-ins read a parameter's value once a call, so
  Kalibre's wrapper (`TimedPlugin`) plays the block in pieces, cut at every
  point of the lane and every 16 samples along a ramp, each piece with its
  parameters' values at its start. A step lands on its sample; a ramp is
  followed to within 16 samples. MIDI in the block goes with its piece.

## Consequences

Tested: a mute and a parameter step at a point falling inside a block, on
the very sample; a send ramp to within 0.01 dB; a parameter ramp within its
16-sample pieces. A plug-in that does its own smoothing still smooths.
Smaller pieces cost a little more CPU while a lane is moving.
