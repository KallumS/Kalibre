# 0067. Instant Virtual Instrument, built in

## Context

One of the repos asked for as devices (0066): Instant Virtual Instrument,
a JUCE plug-in (one recorded note in, a whole sampled instrument out). Its
source makes a plain JUCE AudioProcessor; Kalibre's devices are plug-in
instances.

## Decision

- **Its source, unchanged** (`ThirdParty/InstantVirtualInstrument/Source`,
  from its main branch at 5c26793, GPL-3.0), compiled into Kalibre's app
  library, its own folder its include root and its plug-in name defined on
  its files only, so nothing of Kalibre's is in its way. (Not a library of
  its own: JUCE's modules compile into whatever uses them, and would have
  been there twice.)
- **HostedProcessor** (`Engine/HostedProcessor.*`) makes a processor a device:
  buses, sound, MIDI, state and latency straight through; each parameter
  passed on one for one, as a plug-in format's wrapper does (set, then its
  listeners told, its own saved-state tree among them: without that its
  state didn't keep a value set from Kalibre, found by the test); and its
  window inside the device's, which tells the plug-in before closing it
  (0056).
- Add > Kalibre > Instant Virtual Instrument.

## Consequences

Tested: its parameters (15 and more) are the device's, a value set from
Kalibre reaches it and comes back from its saved state; its window opens,
closes and opens again; with its demo loaded (in the background, as in the
plug-in) a held note sounds. Rendered in the rack (render 22).
