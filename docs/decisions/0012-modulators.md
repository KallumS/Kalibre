# 0012. Modulators

## Context

Movement without drawing automation.

## Decision

A track has modulators: LFO (six shapes, free or synced to the song), ADSR and ADR envelopes started by the track's notes, an envelope follower on this or another track's level, and a wavetable whose cycle can be drawn or read from a sample. Each pushes device parameters up and down by an amount from where they sit. Add one to the last touched control from the Automation menu or a plug-in's menu.

## Consequences

They run once per block (about 10 ms), which is smooth for most uses; audio-rate modulation is not the aim.
