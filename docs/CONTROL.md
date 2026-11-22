# Sync and control surfaces

Set up under **File > Studio > Sync and Control Surfaces…**, which opens the
**Studio** window. These settings belong to this Mac, not to a song.

## Sync (decision 0034)

| | |
| --- | --- |
| **Send to** | Tick the MIDI outputs that should follow Kalibre. |
| **Clock and Song Position** | 24 clock pulses a beat, with Start, Stop, Continue and Song Position. Going round the loop sends Stop, the loop's start, Continue. |
| **Timecode (MTC)** | Quarter-frame timecode to the same outputs, at 24, 25, 29.97 drop or 30 fps; a full-frame message on every locate. |
| **Follow** | A MIDI input whose clock Kalibre follows: its tempo (while the song has one tempo), Start, Continue, Stop and Song Position. |
| **Ableton Link** | Tempo shared over the network with Live, apps and other computers. With **Share Start and Stop**, pressing Play anywhere starts everyone; with others in the session, Kalibre starts on the session's next bar. A song with tempo changes keeps its own tempos. |

## Mackie Control (decision 0035)

Choose the surface's MIDI ports under **Mackie in** and **Mackie out**. Most
surfaces have an MCU (Mackie Control) mode: Behringer X-Touch, iCON
Platform, PreSonus FaderPort 8 and 16, and others.

| On the surface | Does |
| --- | --- |
| Faders 1-8, master | Track volume (the master fader the master); motorised to follow. A move from touch to release is one undo step. |
| V-Pots | Pan; push to centre. The ring shows it. |
| Rec, Solo, Mute, Select | As on the track; their lights follow. |
| Bank left / right | The strips move eight tracks along; Channel left / right, one. |
| Play, Stop, Record, Cycle | The transport and the loop; lit while on. |
| Rewind, Fast Forward | The previous and next bar. |
| Jog wheel | A beat a click. |
| Up, Down | The track above or below. |
| Undo, Save | As in the Edit and File menus. |
| Display | Track names on top, volume underneath. |
| Timecode display | Bar, beat, sixteenth and ticks. |
| Meters | Each strip's level. |

## OSC (decision 0035)

Kalibre listens on **Listen on** (8000 to start with) and answers on **Reply
to** and **Reply port** (127.0.0.1:9000). Tracks are counted from 1 as they're
listed.

| Address | Argument | Does |
| --- | --- | --- |
| `/kalibre/play`, `/kalibre/stop`, `/kalibre/record` | | The transport |
| `/kalibre/undo`, `/kalibre/redo` | | |
| `/kalibre/loop` | 0 or 1 | The loop off or on |
| `/kalibre/tempo` | BPM | The song's tempo |
| `/kalibre/locate` | bar | Move the cursor to a bar |
| `/kalibre/track/N/volume` | 0-1 | Volume, on the fader's scale (0.909 is 0 dB) |
| `/kalibre/track/N/pan` | -1 to 1 | |
| `/kalibre/track/N/mute`, `/solo`, `/arm` | 0 or 1 | |
| `/kalibre/track/N/select` | | Choose the track |

Kalibre sends back, when they change: `/kalibre/playing`, `/kalibre/recording`,
`/kalibre/position` (bar.beat.tick), `/kalibre/tempo`, and for the first 32
tracks `/kalibre/track/N/name`, `/volume`, `/pan`, `/mute`, `/solo`.

## HUI (decision 0043)

Choose the surface's ports under **HUI in** and **HUI out** in the Studio window.
Kalibre pings it every second so it stays online.

| On the surface | Does |
| --- | --- |
| Fader (touch-sensitive) | The strip's volume; touch to release is one undo step |
| V-Pot | Pan |
| Mute, Solo, Rec, Select | The strip's track |
| Bank and Channel left/right | Move the eight strips along |
| Play, Stop, Record, Rewind, Fast Forward | The transport (rewind and forward go a bar) |

The strips' displays show four letters of each track's name, and the meters
the track's level.

## Timecode and Machine Control (decision 0043)

- **Chase MTC**: choose the input whose timecode Kalibre follows. It starts
  where the timecode is, stays within a tenth of a second, and stops when the
  timecode stops.
- **Machine Control (MMC)**: Kalibre sends locate and play when it starts and
  stop when it stops, to the ports ticked for clock.
- **Obey Machine Control**: play, stop, locate and record from any input.
- **Network MIDI**: switches on the Mac's own network MIDI session; another
  computer or an iPad connects to it, and it appears as a MIDI port (click
  Look Again).
