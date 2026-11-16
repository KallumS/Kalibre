# 0039. Kalibre's own synth and sampler; presets, A/B, offline effect settings

## Decision

- **Kalibre Synth**: two band-limited oscillators and noise, a resonant low-pass with its own envelope and key tracking, an amp envelope, glide, 16 voices, mod-wheel vibrato. **Kalibre Sampler**: one sample across the keyboard from its root, start, end, loop, filter, envelope, 32 voices; drop a file on it. Both are ordinary plug-ins (parameters, state, editors in the lower zone).
- **Presets**: the plug-in's own state in a `.kpreset` file per plug-in in Application Support, so it works for AU, VST3, CLAP and Kalibre's own; the plug-in's factory programs beside them; the Presets button on every device and the browser's Presets tab.
- **A/B**: a device keeps the other setting (`Device::stateB`); Set A/Set B swaps. Loading a preset, a program or switching A/B is one undo step, and Undo puts the plug-in's state back (`Device::stateVersion` marks Kalibre's own changes, so ordinary knob turns aren't undone by unrelated steps).
- **Offline effects' settings**: clip menu > Offline Effects > Edit Their Settings opens the chain's plug-ins in the Devices tab; Done keeps their settings, Render renders.
- Engine: a device replaced under the same id is rewired (the wiring key includes each node's own number).
