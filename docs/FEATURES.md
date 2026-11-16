# What Kalibre does, and what it doesn't yet

Every feature asked for, then the list of basic DAW features
(Basic_DAW_features.docx), each marked:

- **Yes**: built and working; tested where it says so.
- **Built, untried**: written, compiles, but needs a Mac with real plug-ins to
  be sure (this was built in a Linux container; CI builds the Mac app).
- **Partly**: the core is there; what's missing is said.
- **Not yet**.

## The features asked for

| Feature | Status | Where, and notes |
| --- | --- | --- |
| Apple silicon Mac app | Yes, on CI | Built for arm64 on every push, with the tests passing on macOS; download Actions > Build > Kalibre-macOS. Not yet opened by the user |
| Hosts AU, VST3 and CLAP | Built, untried | AU and VST3 through JUCE; CLAP through Kalibre's own host (0021). Kalibre menu > Scan for Plug-ins. Each plug-in is scanned in a helper: one that crashes, hangs or won't open is turned off and the scan carries on (0047). Tried on the Mac: the first scan crashed Kalibre, which led to 0047 |
| Plug-in Manager | Yes | Kalibre > Plug-in Manager… (or View): every plug-in and every one turned off, with why; search, sort, scan again, turn off and on, show in Finder. Its own window (0002 amended). Tested (0047) |
| Everything in one window | Yes | 0002. Small questions appear over the window, inside it. Settings may have their own window (amended 16 November; the Plug-in Manager) |
| App icon | Yes | A K with a calibrated stem and a yellow arm (0048) |
| Quitting ends the program | Yes, untried on the Mac | A watchdog ends it five seconds after shutdown starts, so a plug-in or driver can't keep it running (0049) |
| Tempo and time signature, automatable | Yes | Top bar; Transport > Ramp the Tempo to Here; View > Tempo Lane. Ramps integrated exactly, tested (0004) |
| Scripting | Yes | Lua 5.4, Scripts menu, docs/SCRIPTING.md, tested (0019) |
| Audio and MIDI on the same track | Yes | 0003 |
| Plug-in windows in the lower zone, side by side, resized with the zone | Built, untried | 0002. Resizable plug-ins are resized keeping their shape; others are scaled (VST3 and CLAP honour this; some AUs can't be scaled and show at their own size) |
| Track versions (right-click > New Version) | Yes | Version number on the header; tested (0006) |
| MIDI effects before instruments | Yes | Tested (0007) |
| ARA (Melodyne) | Built, untried | 0028. The track's audio clips become the plug-in's playback regions; its edits are saved with the project. One document per plug-in, so Melodyne's multi-track view shows one track |
| Scale chosen, chord shown at the top, scale shaded in the piano roll | Yes | Top bar (Scale, Chord), chord lane, View > Shade the Scale; ScaleView's reader, tested (0015) |
| Piano roll, drum patterns, step sequencer, Euclidean, notation | Yes | Lower zone > Editor (0009, 0014, 0020) |
| Modulators: LFO, ADSR/ADR, envelope follower, wavetable from samples | Yes | Automation > Modulate; shown in the device rack; tested (0012). Once a block, not audio rate |
| Comping, crossfades, sidechains, MIDI learn, loudness, sync, control surfaces | Yes | Asked for on 16 November: decisions 0029-0035, with MTC in, MMC, HUI and network MIDI added (0043). Sync and surfaces not yet tried with real gear |
| Automation picks up the last touched control | Yes | Plug-in parameters and the mixer's faders and pan knobs. Automation > Automate; Touch/Latch/Write recording (0024) |
| X/Y pad: four plug-ins or four samples | Yes | Add > Kalibre > X/Y Morph; tested with samples (0018) |
| Plug-in delay compensation | Yes | Tested by rendering clicks (0016) |
| Track delay (e.g. -125 ms) | Yes | Track menu > Track Delay…; tested (0011) |
| Articulation maps with a delay per articulation | Yes | Track > Articulation Maps; the piano roll's articulation lane; tested (0013) |
| Deactivate plug-ins, keeping their state | Yes | The power switch; tested (0010) |
| Folders in folders, summing, with plug-ins | Yes | Tested (0005) |
| Retrospective recording, MIDI and audio | Yes | Transport > Capture MIDI / Capture Audio; tested (0017) |
| Warping: granular, spectral, repitch | Yes | All three, from the file's tempo or warp markers, tested (0022). Markers are added at the cursor (right-click > Warp) and dragged along the clip |
| Offline effects on clips | Yes | Right-click a clip > Offline Effects (0026): add plug-ins, Edit Their Settings (their windows in the Devices tab, 0039), Render |
| Piano roll mouse rules | Yes | Exactly as asked, tested (0009) |
| Menus in the title bar | Yes | The Mac's menu bar (0027) |
| Apple's guidelines and the house colours | Yes | docs/apple-hig-review.md, docs/COLOUR.md; contrast tested |

## The basic DAW features

| Area | Status | Missing |
| --- | --- | --- |
| 1. Transport | Yes | Play, stop, record, return to start, loop, punch, overdub (recording adds clips), count-in, metronome (accent, subdivisions, volume), tempo, time signature, tap tempo, pre-roll, follow, playhead, bars/beats, minutes, samples and timecode. Pause isn't separate from stop |
| 2. Tracks | Yes | Tracks, folders, returns, master; name, colour, volume, pan, mute, solo, arm, input monitoring, enable, lock, delay, sends, outputs, freeze. Track groups (volume, pan, mute, solo, arm together, 0037). Audio input, MIDI input and channel, and output chosen in the track menu. A video on the timeline with its own tab (0046; the picture on the Mac only) |
| 3. MIDI input and editing | Yes | Every keyboard is listened to; overdub, capture, draw/move/resize/delete/duplicate, quantise, transpose, velocity, split, join, legato, humanise, scale shading, controller lanes. MPE: a channel a note, each with its own bend, pressure and slide, recorded and drawn (0042). Per-track MIDI channel in the track menu |
| 4. Audio recording and editing | Yes | Multitrack recording, punch, split, duplicate, trim, fades, reverse, time-stretch, pitch-shift, clip gain, takes and comping (0029), crossfades (0030). Normalise, Strip Silence, transient detection, Quantise Audio, a sample editor with a spectrogram (0036); tested |
| 5. Arrangement | Yes | Ruler, grid, snap, loop, markers, zoom, clips, track versions. Sections (move, duplicate, delete with their bars), insert/delete/duplicate bars, ripple delete, linked clips, a clip launcher with scenes (0037); tested |
| 6. Mixer and routing | Yes | Faders, pan, mute, solo, polarity, mono, peak and RMS meters, inserts, sends, returns, folders as buses, PDC, sidechains for AU, VST3 and CLAP. Hardware inserts with a measured round trip, instruments' further outputs on tracks, surround (quad, 5.1, 7.1) with multichannel export (0038, 0040); tested |
| 7. Automation | Yes | Lanes, breakpoints, curves, Read/Touch/Latch/Write/Trim (Trim keeps the shape, moved by the hand), last touched, tempo automation, mix moves written, copy and paste, mute and send lanes played (0038); tested |
| 8. Instruments | Yes | Any AU/VST3/CLAP instrument; Kalibre Synth and Kalibre Sampler (0039); the X/Y morph; tested |
| 9. Plug-in hosting | Yes | Scanning in a helper, so a crashing plug-in is turned off instead of closing Kalibre; the Plug-in Manager (0047). Search, bypass, remove, reorder, state, latency, last touched, MIDI learn. Presets (saved per plug-in, the plug-in's own programs, a browser tab) and A/B compare, each undoable (0039) |
| 10. Audio engine | Yes | Device, rate, buffer, inputs and outputs, offline rendering, PDC. CPU per track in the mixer, the whole load as a percentage in the top bar (0038) |
| 11. Projects | Yes | New, open, save, autosave, a JSON format that survives versions. Templates, Collect All Files and Save, Relink Missing Files, archives (0041); tested |
| 12. Export | Yes | Mix and tracks as WAV, AIFF, FLAC or MP3 (320 kb/s), 16/24/32-bit, dither, normalise, loudness targets, MIDI. Song Details written into WAV and MP3; Export Several at Once (0041); tested. FLAC and AIFF carry no tags |
| 13. Sync and control | Yes | MIDI clock in and out, Song Position, MTC out and chased in, MMC out and in, Ableton Link, Mackie Control, HUI, OSC, the Mac's network MIDI from the Studio tab (0034, 0035, 0043). Not yet tried with real gear |
| 14. Metering | Yes | Peak and RMS meters, loudness, true peak and correlation (0033); spectrum, oscilloscope, phase scope and tuner in the Meters tab (0044) |
| 15. Workflow | Yes | Keys for everything, every one changeable (the browser's Keys tab), workspaces (View > Workspaces) (0045), undo history, context menus, drag and drop, Text Size |
| 16. Collaboration | Partly | Audio and MIDI import/export, stems, Final Cut XML out and in, archives (0041). AAF and OMF aren't written: they need the AAF SDK's binary container; XML carries the clips to Resolve, Premiere and Logic |
