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
| Apple silicon Mac app | Yes, on CI | Built for arm64 on every push, with the tests passing on macOS; download Actions > Build > Kalibre-macOS. Used by the user since 16 November: the first scan crashed it (fixed, 0047), and their reports since are fixed (0049, 0056) |
| Hosts AU, VST3 and CLAP | Yes, on the Mac | AU and VST3 through JUCE; CLAP through Kalibre's own host (0021). Kalibre menu > Scan for Plug-ins. Each plug-in is scanned in a helper: one that crashes, hangs or won't open is turned off and the scan carries on (0047). Tried on the Mac: the first scan crashed Kalibre, which led to 0047 |
| Plug-in Manager | Yes | Kalibre > Plug-in Manager… (or View): every plug-in and every one turned off, with why; search, sort, scan again, turn off and on, show in Finder. Its own window (0002 amended). Tested (0047) |
| Everything in one window | Yes | 0002. Small questions appear over the window, inside it. Settings may have their own window (amended 16 November; the Plug-in Manager) |
| App icon | Yes | A K with a calibrated stem and a yellow arm (0048) |
| Quitting ends the program | Yes, not yet confirmed on the Mac | A watchdog ends it five seconds after shutdown starts, so a plug-in or driver can't keep it running (0049) |
| Tempo and time signature, automatable | Yes | Top bar; Transport > Ramp the Tempo to Here; View > Tempo Lane. Ramps integrated exactly, tested (0004) |
| Scripting | Yes | Lua 5.4, Scripts menu, docs/SCRIPTING.md, tested (0019) |
| Audio and MIDI on the same track | Yes | 0003 |
| Plug-in windows in the lower zone, side by side, resized with the zone | Built, untried on the Mac | 0002, 0056, 0058. Each fitted to the zone, its shape kept: resizable ones asked to take the size, others shown smaller (never stretched, never bigger); clipped to the rack; two-finger swipes move along the rack; the ⧉ button moves one into a window of its own and back; Match Loudness on each effect. Kept when devices are added (they went blank before) |
| Track versions (right-click > New Version) | Yes | Version number on the header; four at most, each with its own plug-ins, chosen with the mixer's 1 to 4 buttons, several tracks at once with Shift-click; tested (0006, 0050) |
| Summing track | Yes | Track > Add Summing Track: armed, it records the master from where recording starts; tested (0052) |
| Shift and Option to choose | Yes | Shift-click a range, Option-click adds, Shift-Option-click takes out: tracks (arrangement and mixer), their buttons, clips, notes and the Plug-in Manager; tested (0009, 0053) |
| MusicXML | Yes | File > Import MusicXML… (.musicxml, .xml, .mxl) and Export MusicXML…, for Dorico, MuseScore, Sibelius, Finale; tested (0057). Triplets come out as 1/32s; markings and lyrics aren't carried |
| Container | Yes | Add > Kalibre > Container: up to eight plug-ins in a row, each with On, Solo, Stereo/Mid/Side, a band, volume and pan; Match Loudness by LUFS-S; tested (0055) |
| Snap to zero crossings | Yes | Edit > Snap to Zero Crossings, or the sample editor's button: clip edges, splits and sample-editor ranges land on the exact sample where the waveform crosses zero; tested (0054) |
| Cue track | Yes | Track > Add Cue Track: after the master, its plug-ins, pan and mono heard but never exported; tested (0051) |
| MIDI effects before instruments | Yes | Tested (0007) |
| ARA (Melodyne) | Built, untried | 0028. The track's audio clips become the plug-in's playback regions; its edits are saved with the project. One document per plug-in, so Melodyne's multi-track view shows one track |
| Scale chosen, chord shown at the top, scale shaded in the piano roll | Yes | Top bar (Scale, Chord), chord lane, View > Shade the Scale; ScaleView's reader, tested (0015) |
| Arm for audio or MIDI | Yes | Click the arm button: red, records audio; right-click: blue, records MIDI, what the instrument is played, arpeggiators and MIDI effects included (or what it sends of its own); tested (0064) |
| Swing, gate and repeat | Yes | Swing and gate sliders in the drum grid, step sequencer and Euclidean, saved with each clip; Repeat (1-8 per step) in the drum grid and step sequencer's Velocity ▾ menu; tested (0063) |
| Random patterns | Yes | Random in the drum grid (a Good Idea groove), the step sequencer (a Good Idea motif in the song's key) and the Euclidean (new rhythms for every drum): a new one each click, one undo step; tested (0062) |
| Note chance, random velocity, random timing | Yes | Click Velocity ▾ in the piano roll (or the button in Steps and Drums): each note's chance of playing, a velocity range and a timing range (± ms), chosen afresh each time it plays, in exports too; tested (0061) |
| CC lanes and AutoCC | Yes | Under the piano roll and notation, any of the 128 controllers by their standard names (CC Lanes…); Auto draws CC1, CC11, CC7 and CC21 as AutoCC plays the notes, on a track named for its instrument; tested against AutoCC's own numbers (0060). AutoCC's Mode, Trigger, Depth, Time and Dynamic not yet shown |
| Piano roll, drum patterns, step sequencer, Euclidean, notation | Yes | Lower zone > Editor (0009, 0014, 0020). All add to the same clip, each editor showing its own notes, the piano roll all of them; the step being played is lit; tested (0059) |
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
| Piano roll mouse rules | Yes | Exactly as asked, tested (0009); choosing notes as everywhere else since 16 November (0053); a new note is as long as the last one added, clicked or stretched |
| Menus in the title bar | Yes | The Mac's menu bar (0027) |
| Apple's guidelines and the house colours | Yes | docs/apple-hig-review.md, docs/COLOUR.md; contrast tested |

## The basic DAW features

| Area | Status | Missing |
| --- | --- | --- |
| 1. Transport | Yes | Play, stop, record, return to start, loop, punch, overdub (recording adds clips), count-in, metronome (accent, subdivisions, volume), tempo, time signature, tap tempo, pre-roll, follow, playhead, bars/beats, minutes, samples and timecode. Pause isn't separate from stop |
| 2. Tracks | Yes | Tracks (right-click under them to add one, 0058), folders, returns, master; name, colour, volume, pan, mute, solo, arm, input monitoring, enable, lock, delay, sends, outputs, freeze. Track groups (volume, pan, mute, solo, arm together, 0037). Audio input, MIDI input and channel, and output chosen in the track menu. A video on the timeline with its own tab (0046; the picture on the Mac only) |
| 3. MIDI input and editing | Yes | Every keyboard is listened to; overdub, capture, draw/move/resize/delete/duplicate, quantise, transpose, velocity, split, join, legato, humanise, scale shading, controller lanes. MPE: a channel a note, each with its own bend, pressure and slide, recorded and drawn (0042). Per-track MIDI channel in the track menu |
| 4. Audio recording and editing | Yes | Multitrack recording, punch, split, duplicate, trim, fades, reverse, time-stretch, pitch-shift, clip gain, takes and comping (0029), crossfades (0030). Normalise, Strip Silence, transient detection, Quantise Audio, a sample editor with a spectrogram (0036); tested |
| 5. Arrangement | Yes | Ruler, grid, snap, loop, markers, zoom, clips, track versions. Sections (move, Shift-drag to any bar, duplicate, delete with their bars), insert/delete/duplicate bars, ripple delete, linked clips, a clip launcher with scenes (0037); tested |
| 6. Mixer and routing | Yes | Faders, pan, mute, solo, polarity, mono (a button on every strip), versions 1 to 4, a Cue track after the master (0050, 0051), peak and RMS meters, inserts, sends, returns, folders as buses, PDC, sidechains for AU, VST3 and CLAP. Hardware inserts with a measured round trip, instruments' further outputs on tracks, surround (quad, 5.1, 7.1) with multichannel export (0038, 0040); tested |
| 7. Automation | Yes | Lanes, breakpoints, curves, Read/Touch/Latch/Write/Trim (Trim keeps the shape, moved by the hand), last touched, tempo automation, mix moves written, copy and paste, mute and send lanes played (0038); tested |
| 8. Instruments | Yes | Any AU/VST3/CLAP instrument; Kalibre Synth and Kalibre Sampler (0039); the X/Y morph; tested |
| 9. Plug-in hosting | Yes | Scanning in a helper, so a crashing plug-in is turned off instead of closing Kalibre; the Plug-in Manager (0047). Search, bypass, remove, reorder, state, latency, last touched, MIDI learn. Presets (saved per plug-in, the plug-in's own programs, a browser tab) and A/B compare, each undoable (0039) |
| 10. Audio engine | Yes | Device, rate, buffer, inputs and outputs, offline rendering, PDC. CPU per track in the mixer, the whole load as a percentage in the top bar (0038) |
| 11. Projects | Yes | New, open, save, autosave, a JSON format that survives versions. Templates, Collect All Files and Save, Relink Missing Files, archives (0041); tested |
| 12. Export | Yes | Mix and tracks as WAV, AIFF, FLAC or MP3 (320 kb/s), 16/24/32-bit, dither, normalise, loudness targets, MIDI. Song Details written into WAV and MP3; Export Several at Once (0041); tested. FLAC and AIFF carry no tags |
| 13. Sync and control | Yes | MIDI clock in and out, Song Position, MTC out and chased in, MMC out and in, Ableton Link, Mackie Control, HUI, OSC, the Mac's network MIDI from the Studio tab (0034, 0035, 0043). Not yet tried with real gear |
| 14. Metering | Yes | Peak and RMS meters, loudness, true peak and correlation (0033); spectrum, oscilloscope, phase scope and tuner in the Meters tab (0044) |
| 15. Workflow | Yes | Keys for everything, every one changeable (the browser's Keys tab), workspaces (View > Workspaces) (0045), undo history, context menus, drag and drop, Text Size |
| 16. Collaboration | Partly | Audio and MIDI import/export, MusicXML in and out (0057), stems, Final Cut XML out and in, archives (0041). AAF and OMF aren't written: they need the AAF SDK's binary container; XML carries the clips to Resolve, Premiere and Logic |
