# 0033. Loudness meters and loudness-matched export

## Context

Streaming services turn songs to a loudness; a mix should be checked, and delivered, by loudness rather than peak.

## Decision

The master measures loudness as ITU-R BS.1770-4 and EBU R128 define it: momentary (400 ms), short-term (3 s), integrated (gated at -70 LUFS and 10 LU under the average), loudness range (10th to 95th percentile of short-term, gated 20 LU under), true peak (read at 4x between samples) and stereo correlation. It's shown in the mixer beside the master, with a minute of short-term history against lines at -14 (streaming) and -23 (broadcast), and Reset. File > Export the Mix at a Loudness renders at -14, -16 or -23 LUFS, never letting the true peak past -1 dBTP: if it would, the export is quieter and the message says so.

## Consequences

Tested: a 997 Hz tone at -20 dBFS in both channels reads -20.0 LUFS at 44.1, 48 and 96 kHz; quiet passages under the gate don't pull the integrated reading down; a tone whose samples sit at 0.707 shows a true peak of 0 dBTP; an export aimed at -14 measures -14.
