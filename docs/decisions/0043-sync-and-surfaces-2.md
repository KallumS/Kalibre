# 0043. MTC in, MIDI Machine Control, HUI, network MIDI

## Decision

- **Chase MTC** (Studio tab): quarter frames are put back together (`sync::MtcReader`, tested); Kalibre starts where the timecode is, relocates when it drifts more than 0.1 s, stops when it stops.
- **MMC out**: locate and play when Kalibre starts, stop when it stops, to the clock ports. **MMC in** (Obey Machine Control): play, stop, locate, record.
- **HUI** (`Core/Hui`, tested): faders (14-bit, with touch), mute, solo, record, select, V-Pots for pan, banks, the transport, four-letter names, meters, and the ping that keeps the surface online.
- **Network MIDI**: the Studio tab switches the Mac's own network session on (CoreMIDI's MIDINetworkSession), open to anyone on the network.

## Consequences

- None of these have been tried with real gear yet. HUI's zone and port numbers follow the published reverse-engineered map.
