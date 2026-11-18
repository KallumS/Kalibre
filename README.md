# Kalibre

A DAW for Apple silicon Macs, made to be exactly right for one musician, and
so maybe for others. Everything happens in one window: the plug-ins you use
sit side by side under your tracks, their own windows inside Kalibre's, and
grow and shrink with it.

![The window: tracks and folders, the chord lane, and the X/Y morph in the device rack](docs/window.png)

## What's in it

- **AU, VST3 and CLAP** plug-ins, hosted side by side, with **plug-in delay
  compensation** so they stay in time; switch one **off** and it's unloaded
  from memory with its settings kept. Each is **fitted to the rack**, shown
  smaller if it must be, never stretched; swipe sideways with two fingers to
  move along the rack. Each can move to **its own window**, and **Match
  Loudness** keeps an effect's output as loud as its input.
- **Every performance core**: the tracks' plug-ins play side by side on all
  the Mac's fast cores, as audio threads, exactly as they'd sound on one.
- A **plug-in scanner** that looks at each plug-in in a helper, so one that
  crashes is turned off instead of taking Kalibre with it, and learns which
  send MIDI, so MIDI generators like Riffer are filed as MIDI effects; the
  **Plug-in Manager** shows them all and files any one by hand.
- **Crash reports** that name the plug-in that was playing, and **backups**
  of the song every 15 minutes.
- **The Container**: up to eight plug-ins in a row, each on its own band,
  the mid or the sides, with on, solo, volume and pan.
- **Audio and MIDI on the same track.** Folders inside folders that sum their
  tracks and take plug-ins of their own. Returns and sends. Each track's
  inputs and outputs on its **I/O** button.
- **Track versions**: up to four a track, each with its own clips and its own
  plug-ins; switch with the 1 to 4 buttons in the mixer, several tracks at
  once, to compare them A/B/C/D.
- **A Summing track** that records the master as it plays, and a **Cue
  track** after the master whose plug-ins, pan and mono you hear but never
  export.
- **MIDI effects** (Scaler, arpeggiators, sequencers) always run before the
  instrument.
- **The family's own tools, built in** (Add > Kalibre): Midi Evolver,
  Transient Shuffler, Reese Basser, Reverberator and Instrument Creator (the
  JSFX REAPER runs); Instant Virtual Instrument; Starting Blocks, Starting
  Blocks Notation and Midi Catalogue, whose Insert at Cursor puts the block
  in the song; and Orchestration Helper beside the track. Each in its own
  window, as in REAPER.
- **The scale and the chord you're playing** at the top of the window; the
  scale shaded in the piano roll; the chords of the clips you ask
  (**Detect Chords**) in a lane along the timeline.
- **A message line** that says what just happened, then what's chosen or
  under the pointer: a note's pitch, velocity and length, a clip's length and
  notes. **The top bar** shows what you choose (right-click it), with the
  arrangement's **grid** and **Follow** that scrolls or turns the page.
- **Five ways to write notes**: the piano roll (click to add, right-click to
  remove, drag to move or stretch, right-drag to clear an area, Shift-drag to
  choose; a new note is as long as the last one), a drum grid, a step
  sequencer, a Euclidean sequencer, notation, **all adding to the same
  clip**, each showing its own notes and lighting the step being played.
  **Step input** from a MIDI keyboard (hold the keys, press 1-8 for the
  length, let go; - for a rest). **Select** the roots, thirds or fifths of every chord,
  the notes on the beat, the top line; **Explode Chords** into a track a
  voice. **MusicXML** and **DAWproject** (Bitwig, Studio One, Cubase) in and
  out, automation and warping included.
- **Note Splitter**: a chord's notes, top to bottom, each to another track's
  MIDI input, so one chord plays four string sections.
- **Fix Note Timings and Volume** (right-click a MIDI clip): Note Position &
  Volume Fixer built in. For sloppily edited orchestral libraries, it listens
  to the track, finds where each note is really heard and how loud, and puts
  each sound on the grid and evens out its level within its articulation.
- **Arm for audio or MIDI**: click a track's arm button for audio (red),
  right-click for MIDI (blue), which records what its instrument is played,
  a free-running arpeggiator's notes included; Shift-click for both,
  Shift-right-click for the MIDI as it comes in.
- **Swing, gate and repeat** in the drum grid and step sequencer (swing
  and gate in the Euclidean too): a step can play up to eight times in its
  length, for flams, rolls and ratchets.
- **Random** in the drum grid, step sequencer and Euclidean: a new groove,
  tune or set of rhythms every click, made by **Good Idea**'s engine.
- **Note chance, random velocity and random timing** for every note, in the
  piano roll, the step sequencer and the drum grid: chosen afresh each time
  it plays.
- **CC lanes** under the piano roll and notation, any controller by its
  name, and **Auto**: on a track named for its instrument (Violins, Horns,
  Flute), it draws the dynamics, expression, volume and vibrato lanes as
  **AutoCC** would play the notes.
- **Choosing** is the same everywhere: Shift-click a range, Option-click to
  add, Shift-Option-click to take away.
- **Tempo and time signature** that change and ramp along the song.
- **Modulators**: LFO, ADSR and ADR, envelope follower, and wavetables drawn
  or read from a sample.
- **Automation that picks up the last thing you touched.**
- **The X/Y morph**: four plug-ins or four samples, one in each corner.
- **Track delay** (-125 ms to pull slow strings into time) and **articulation
  maps** with a delay for each articulation, chosen in the piano roll;
  Cubase, Logic and Reaticulate maps imported in the **Articulation Manager**,
  where each articulation's delay is set and kept; a **delay for each note**
  too.
- **Capture**: it's always listening, so the idea you played before pressing
  Record can still be kept, MIDI or audio.
- **Warping**: repitch, granular or spectral, from the loop's tempo or warp
  markers. **Offline effects** on single clips.
- **ARA** plug-ins like Melodyne read the track's audio clips directly.
- **Takes and comping**: every pass of a loop is a take; drag across a take
  to use it there. **Crossfades** wherever audio overlaps.
- **Sidechains** from any track into a plug-in. **MIDI learn** for any knob.
- **Loudness meters** (LUFS, true peak, range) and export at -14 LUFS.
- **Sync**: MIDI clock and timecode out, clock and timecode in, Machine
  Control, Ableton Link, the Mac's network MIDI.
- **Control surfaces**: Mackie Control (X-Touch, FaderPort...), HUI and OSC.
- **Bounce in Place** for clips, and **Freeze** for tracks (quietly, nothing
  else reloaded; Unfreeze deletes the render and brings the plug-ins back).
  Everything Kalibre records or renders, and all audio brought in, kept as
  **32-bit float WAV**; exports in any format.
- **A clean engine**: 32-bit float processing with **64-bit summing**
  wherever signals meet, so mixing, routing, unstretched clips, splits,
  bypassed effects, freezing and recording leave the sound exactly as it
  was, each proved by a cancellation test. Constant-power panning, 0 dB in
  the middle; volume and pan automation sample by sample; **Consolidate** for audio
  clips.
- **Audio tools**: normalise, strip silence, quantise audio to the grid, a
  sample editor with a spectrogram, **snap to zero crossings**.
- **Sections** you move (Shift-drag one to any bar) and duplicate with all their music, ripple editing, **alias clips** (edit one, every copy follows),
  track groups, and a **clip launcher** for playing ideas in loops.
- **Kalibre Synth and Sampler**, **presets** and **A/B** for every plug-in,
  hardware inserts, instruments' extra outputs on their own tracks,
  **surround** up to 7.1, **MPE**.
- **Meters**: peak and RMS, spectrum, oscilloscope, phase scope, a tuner;
  CPU for each track.
- **Files**: templates, collect and save, relink, archives, MP3 with the
  song's details, several exports at once, Final Cut XML; a **video** track.
- **Every key changeable**, and saved **workspaces**.
- **Lua scripts**, as in REAPER.
- Menus in the Mac's menu bar; the house colours; Apple's guidelines.

![The mixer: versions 1 to 4, Mono on every strip, the Cue track after the master](docs/mixer-versions-and-cue.png)
![The Container](docs/container.png)
![The Plug-in Manager](docs/plugin-manager.png)
![The Articulation Manager: Cubase, Logic and Reaticulate maps imported, two articulations given delays](docs/articulation-manager.png)
![The Note Splitter: a chord's notes to Violins I, Violins II and Violas](docs/note-splitter.png)
![Fix Note Timings and Volume: how to fix them](docs/fix-note-timings-and-volume.png)
![The top bar, its Grid, and the message line telling what's chosen; I/O on each track](docs/top-bar-and-io.png)
![The sample editor](docs/sample-editor.png)
![Kalibre Synth and Sampler](docs/synth-and-sampler.png)
![A plug-in shown smaller to fit a short rack](docs/fitted-to-the-rack.png)
![The step sequencer lighting the step being played](docs/steps-playing.png)
![Auto: a violin line's CC lanes, drawn as AutoCC plays strings](docs/auto-cc.png)
![Arming four ways: audio (red), MIDI (blue), both, MIDI input](docs/arm-four-ways.png)
![Reese Basser and Reverberator in the rack](docs/jsfx-devices.png)
![Instant Virtual Instrument](docs/instant-virtual-instrument.png)
![Starting Blocks](docs/starting-blocks.png)
![Midi Catalogue](docs/midi-catalogue.png)
![Orchestration Helper, "trombone" searched](docs/orchestration-helper.png)
![The clip launcher](docs/launcher.png)
![The Meters tab](docs/meters.png)

What's done and what isn't, feature by feature: **[docs/FEATURES.md](docs/FEATURES.md)**.
**ARA** (Melodyne) is built in, but hasn't been tried with Melodyne on a Mac yet.

## Get it

Every push is built for Apple silicon by GitHub Actions: open the latest
**Build** run under the repository's **Actions** tab and download
**Kalibre-macOS**. Open the `.dmg`, drag Kalibre into Applications, and the
first time right-click it and choose **Open**. Then **Kalibre > Plug-in
Manager…** and **Scan for New Plug-ins**.

Or build it on your Mac: double-click `build-mac.command` (needs Xcode's
Command Line Tools and CMake).

## For developers

```sh
cmake -B build-core -G Ninja -DKALIBRE_BUILD_APP=OFF && cmake --build build-core && ./build-core/KalibreTests
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build
```

[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) is the map;
[docs/decisions/](docs/decisions/README.md) the reasons;
[docs/SCRIPTING.md](docs/SCRIPTING.md) the script API.

Licence: AGPLv3, as JUCE's open-source licence is.
