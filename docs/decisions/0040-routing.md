# 0040. Surround, instrument outputs, hardware inserts, CLAP sidechains

## Decision

- **Surround**: Track > Mix Format: stereo, quad, 5.1 or 7.1 (channel order L R C LFE then surrounds). Faders carry eight channels; a track is placed by pan (left-right) and front-back (a second knob in the mixer) between the two speakers either side, at constant power (`Core/Surround`, tested). Folders and the master pass every channel; their plug-ins take the front pair. Post-fader sends take the stereo sound before the fader and follow its level. Exports have every channel. To hear it, switch on enough outputs in the browser's Audio tab.
- **Instrument outputs**: an instrument with more than two outputs has them switched on; device menu > Its Other Outputs as Tracks makes a track for each further pair.
- **Hardware inserts**: Add > Kalibre > Hardware Insert sends out of two of the interface's outputs and returns from two inputs. Ping measures the round trip, which becomes the insert's latency, so PDC lines everything up. Exporting can't use hardware: the insert passes the sound through.
- **CLAP sidechains**: a CLAP plug-in's second input port is offered as a sidechain like AU and VST3 (0031).
