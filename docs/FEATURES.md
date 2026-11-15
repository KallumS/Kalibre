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
| Apple silicon Mac app | Built, untried | CI builds an arm64 `.dmg` on every push (Actions > Build > Kalibre-macOS) |
| Hosts AU, VST3 and CLAP | Built, untried | AU and VST3 through JUCE; CLAP through Kalibre's own host (0021). Kalibre menu > Scan for Plug-ins |
| Everything in one window | Yes | 0002. Small questions appear over the window, inside it |
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
| Automation picks up the last touched control | Yes | Plug-in parameters and the mixer's faders and pan knobs. Automation > Automate; Touch/Latch/Write recording (0024) |
| X/Y pad: four plug-ins or four samples | Yes | Add > Kalibre > X/Y Morph; tested with samples (0018) |
| Plug-in delay compensation | Yes | Tested by rendering clicks (0016) |
| Track delay (e.g. -125 ms) | Yes | Track menu > Track Delay…; tested (0011) |
| Articulation maps with a delay per articulation | Yes | Track > Articulation Maps; the piano roll's articulation lane; tested (0013) |
| Deactivate plug-ins, keeping their state | Yes | The power switch; tested (0010) |
| Folders in folders, summing, with plug-ins | Yes | Tested (0005) |
| Retrospective recording, MIDI and audio | Yes | Transport > Capture MIDI / Capture Audio; tested (0017) |
| Warping: granular, spectral, repitch | Yes | All three, from the file's tempo or warp markers, tested (0022). Markers are added at the cursor (right-click > Warp) and dragged along the clip |
| Offline effects on clips | Partly | Right-click a clip > Offline Effects (0026). The chain uses each plug-in's default settings until its window can be opened for it |
| Piano roll mouse rules | Yes | Exactly as asked, tested (0009) |
| Menus in the title bar | Yes | The Mac's menu bar (0027) |
| Apple's guidelines and the house colours | Yes | docs/apple-hig-review.md, docs/COLOUR.md; contrast tested |

## The basic DAW features

| Area | Status | Missing |
| --- | --- | --- |
| 1. Transport | Yes | Play, stop, record, return to start, loop, punch, overdub (recording adds clips), count-in, metronome (accent, subdivisions, volume), tempo, time signature, tap tempo, pre-roll, follow, playhead, bars/beats, minutes, samples and timecode. Pause isn't separate from stop |
| 2. Tracks | Partly | Tracks, folders, returns, master; name, colour, volume, pan, mute, solo, arm, input monitoring, enable, lock, delay, sends, outputs. Not yet: video tracks, freeze, track grouping links, input/output choosers in the window (inputs default to 1-2) |
| 3. MIDI input and editing | Partly | Every keyboard is listened to; overdub, capture, draw/move/resize/delete/duplicate, quantise, transpose, velocity, split, join, legato, humanise, scale shading. Controller lanes in the piano roll (modulation, expression, sustain, pitch bend, pressure, any CC: click the lane's name), drawn freehand, right-drag erases; no MPE; MIDI channel filtering per track not in the window |
| 4. Audio recording and editing | Partly | Multitrack recording to 24-bit WAV, punch, split, duplicate, trim, fades, reverse, time-stretch, pitch-shift, clip gain (in the file), non-destructive. Not yet: comping lanes, crossfades, normalise, silence and transient detection, audio quantise, sample editing, spectral display |
| 5. Arrangement | Partly | Ruler, grid, snap, loop, markers, zoom, scroll, clips (loop, mute, lock, rename, consolidate), track versions. Not yet: arrangement sections, ripple editing, linked editing, clip launching |
| 6. Mixer and routing | Partly | Faders, pan, mute, solo, polarity, mono, peak meters, inserts, sends pre and post, returns, folders as buses, plug-in delay compensation. Not yet: sidechain inputs, hardware inserts, multi-output instruments, surround, RMS meters |
| 7. Automation | Partly | Lanes, breakpoints, linear/stepped/curved, drawing points, Read/Touch/Latch/Write, last touched, tempo automation. Not yet: trim mode's behaviour, copy/paste of automation, mute and send automation playback |
| 8. Instruments | Partly | Any AU/VST3/CLAP instrument; the X/Y morph plays samples. No built-in synth or sampler yet |
| 9. Plug-in hosting | Partly | Scanning (crash-safe), search, by maker, bypass, remove, reorder, state, latency, last-touched. Not yet: presets browser, A/B, MIDI learn |
| 10. Audio engine | Partly | Device, rate, buffer, inputs and outputs (browser > Audio), offline rendering, PDC. Not yet: CPU meters per track, dropout counter |
| 11. Projects | Partly | New, open, save, save as, autosave every three minutes (File > Open the Last Autosave), a JSON format that survives versions. Not yet: templates, collect and save, relink |
| 12. Export | Partly | Mix and every track as WAV, AIFF or FLAC, 16/24/32-bit, dither, normalise, tail, loop range; MIDI file. Not yet: MP3, loudness targets, batch, metadata |
| 13. Sync and control | Not yet | MIDI clock, MTC, Mackie/HUI, OSC, Link |
| 14. Metering | Partly | Peak meters with clip marks. Not yet: LUFS, true peak, spectrum, correlation |
| 15. Workflow | Partly | Keys for everything common, undo history (500), context menus, drag and drop, Text Size. Not yet: customisable keys, workspaces |
| 16. Collaboration | Partly | Audio and MIDI import/export, stems. Not yet: AAF/OMF/XML, archiving |
