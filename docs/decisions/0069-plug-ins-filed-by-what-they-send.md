# 0069. Plug-ins filed by what they send; scanning in the Plug-in Manager

## Context

Testing on the Mac (17 November): MIDI effects that had been under MIDI
Effects were now under Instruments or Audio Effects; Riffer (Audiomodern)
was under Instruments. 0058 had stopped filing every VST3 without channel
counts as a MIDI effect (BitFuzzer, an audio effect, was put before its
instrument), and that rule had been what put the VST3 MIDI effects in the
right place. Riffer, like Scaler and other MIDI generators, is built as an
instrument that sends MIDI, because VST3 has no kind for a MIDI effect: its
description can't tell it from a synth.

Also asked: Scan for Plug-ins only inside the Plug-in Manager, the Plug-in
Manager in one menu only, and the family's devices (Midi Evolver, Starting
Blocks and the rest, 0066-0068) with Kalibre's own instead of under a
"KallumS" heading.

## Decision

- **The scan opens each plug-in**, in the helper (0047), after writing down
  what its description says (so one that crashes when opened is still
  listed as before). It notes whether it sends MIDI, says it's a MIDI
  effect, and its real channels. Kept beside the list (`KALIBRE_TRAITS`).
- **Filed** (`PluginManager::roleOf`): the user's choice first; then a MIDI
  effect by its own say, or anything that sends MIDI and no sound; then the
  description (0058).
- **An instrument that sends MIDI is listed under MIDI Effects too** (and
  added there as one, its sound unused). Riffer, Scaler and Cthulhu are
  found that way, a synth that happens to send MIDI is listed twice.
- **File As**, right-click in the Plug-in Manager: MIDI Effect, Instrument or
  Audio Effect, or As Kalibre Finds It. Kept with the list.
- Plug-ins scanned before this need another look: the next Scan for New
  Plug-ins opens them again, and the message line says so on starting.
- **Scanning lives in the Plug-in Manager** (Scan for New Plug-ins); the
  Plug-in Manager is in the Kalibre menu only (not View). Messages that
  pointed at "Kalibre > Scan for Plug-ins" point at the manager.
- The family's devices are made by "Kalibre" and listed with Kalibre Synth,
  the Container and the others, without a heading of their own.

## Consequences

A scan takes longer: each plug-in is opened as well as read (in the helper,
so a slow or crashing one costs only time). Tested with fake plug-ins: one
that sends MIDI and no sound is a MIDI effect, one that sends MIDI with
outputs is listed in both, a hand filing is kept, an old list is looked at
again. Whether Riffer's own build sends MIDI with outputs or without needs
the Mac; either way it's under MIDI Effects.
