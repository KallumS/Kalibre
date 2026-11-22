# 0117. The Sampler's modes as buttons; its waveform plays

## Context

Asked (22 November): Granular and Slices were hard to find (a Mode knob among
twenty); make Keys, Granular and Slices buttons at the top, each bringing up
its own controls and hiding the rest. And: clicking the waveform should play
the sample from there, a new click cutting off the last rather than piling up.

## Decision

- **Keys, Slices, Granular** as buttons along the top left, Load Sample and
  Random Sample on the right; the waveform under them, the panel's width. The
  chosen mode lit. Mode stays a parameter (automation, presets), only its
  knob is gone. Each mode shows its knobs only: Loop and Loop Start in Keys
  and Granular; Slice Sensitivity in Slices; Grain Size, Density, Spray and
  Position in Granular; the rest in all three. The knob panel can hide knobs
  (`KnobPanel::shows`), groups with none left drop out.
- **Click the waveform**: the sample from that point to its end, as it is
  (its own pitch, Volume only, no envelope or filter), through the track like
  anything the Sampler plays. A new click fades the last out in 5 ms as the
  new one fades in over 2 ms: one at a time, never clicking. A yellow line
  follows it (the editor redraws 30 times a second).

## Consequences

Tested: a click at a quarter hears the sample's click where it should be; a
second click before the first reaches its click means that click is never
heard; each mode's button shows its knobs and not the others'. Renders 13
and 37.
