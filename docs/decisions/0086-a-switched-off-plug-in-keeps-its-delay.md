# 0086. A switched-off plug-in keeps its delay

## Context

Switching a plug-in off unloads it (0010), and its latency went with it, so
its track came out that much earlier than the others. Bypass (0081) keeps the
latency; asked (18 November, late) for switching off to do the same.

## Decision

When an effect is switched off, its latency (and the sample rate) is kept in
the song (`Device::offLatency`). The engine puts a plain delay that long
(`HeldDelay`) where it was, reporting that latency, so delay compensation
lines everything up as before; at another sample rate it is scaled. Switched
on, the plug-in itself is back.

## Consequences

Tested: a click through a switched-off 300-sample plug-in still lines up with
a dry one; the delay is saved; switched on, the same. Instruments and MIDI
effects switched off make no sound to line up, so nothing stands in for them.
