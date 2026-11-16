# Architecture

## The shape of it

| | |
| --- | --- |
| `Source/Core/` | The music, with **no JUCE** (decision 0001): tests run in seconds. |
| `Core/TempoMap.*` | Ticks (960 a quarter) to seconds and back through held and ramped tempos; bars through time signatures (0004). |
| `Core/Model.*` | The project as plain data: tracks (folders, returns, the master), versions of clips, MIDI and audio clips, devices, sends, automation, modulators, articulation maps (0003, 0005, 0006). |
| `Core/Edit.*` | Every change as a function, and the undo history (0008). |
| `Core/Roll.*` | The piano roll's mouse rules as a state machine (0009). |
| `Core/Patterns.*` | Drum grid, step sequencer and Euclidean rhythms, read from and written to notes (0014). |
| `Core/Harmony.*`, `ScaleModel.h` | Scales, chord names (ScaleView's, unchanged), the chord lane, key finding (0015). |
| `Core/Playback.*` | A track's clips compiled into MIDI messages and audio regions in seconds, with track and articulation delays (0011, 0013). |
| `Core/Modulation.*` | LFO, envelopes, follower and wavetable state (0012). |
| `Core/Capture.*` | The always-on MIDI ring and the last performance (0017). |
| `Core/Notation.*` | Spelling and bar layout for the staff (0020). |
| `Core/Comp.*` | Takes and comping (0029). |
| `Core/MidiLearn.*` | MIDI learn mappings (0032). |
| `Core/Loudness.*` | BS.1770 / R128 loudness, true peak, correlation (0033). |
| `Core/Sync.*`, `ControlSettings.*` | MIDI clock, MTC, clock following; the studio's settings (0034). |
| `Core/Mackie.*` | The Mackie Control protocol (0035). |
| `Core/AudioTools.*` | Peaks, silence, transients, audio quantise markers, sample edits, FFT, YIN pitch (0036, 0044). |
| `Core/Song.*` | Time edits, sections, ripple, linked clips, track groups, the clip launcher (0037). |
| `Core/Surround.*` | Speaker gains for quad, 5.1 and 7.1 (0040). |
| `Core/ProjectTools.*` | A project's files, relinking, templates, ID3 tags, Final Cut XML (0041). |
| `Core/Mpe.*` | Note expression and MPE playback (0042). |
| `Core/Hui.*` | The HUI protocol (0043). |
| `Core/ProjectFile.*` | `.kalibre` files: JSON with nlohmann/json. |
| `Source/Script/LuaHost.*` | The `kalibre` Lua API (0019). |
| `Source/Engine/` | JUCE's audio side. |
| `Engine/Engine.*` | The audio graph built from the project, plug-in instances kept across changes, the transport and loop, rendering (0016). |
| `Engine/TrackProcessors.*` | Each track's Source (clips, live input, recording, automation, modulators), Fader and Sends; the metronome. |
| `Engine/ClapFormat.*`, `ClapGuiMac.mm` | Hosting CLAP (0021). |
| `Engine/PluginManager.*` | Scanning (each plug-in in a helper: Kalibre run with `--kalibre-scan`), turning plug-ins off and on, and making them (0047). |
| `Engine/MorphProcessor.*` | The X/Y morph (0018), and the latency and gain devices the tests use. |
| `Engine/ContainerProcessor.*` | The Container: plug-ins in a row, each on a band, the mid or the sides (0055). |
| `Engine/AraHost.*` | ARA documents for plug-ins like Melodyne (0028). |
| `Engine/SyncDevices.*` | Clock and timecode out, clock in, Ableton Link (0034). |
| `Engine/Warp.*` | Audio in memory, and warping (0022). |
| `Engine/Swap.h` | Handing new data to the audio thread without locks. |
| `Engine/TimedPlugin.h` | Wraps every plug-in in the graph to time it; `Engine::instance` hands out the plug-in inside (0038). It also parks the plug-ins of versions not playing (0050). |
| `Engine/BuiltinInstruments.*`, `HardwareInsert.*` | Kalibre Synth and Sampler (0039); the hardware insert and the interface's channels it shares (0040). |
| `Engine/Mp3Writer.*` | MP3 export through Shine (0041). |
| `Engine/NetworkMidi*` | The Mac's network MIDI session (0043). |
| `Source/App/` | The window (0002). `Controller` owns the project, undo, the selection and every command; the views ask it. |
| `App/TopBar`, `Arrange`, `LowerZone`, `PianoRoll`, `PatternEditors`, `NotationView`, `DeviceRack`, `Mixer`, `Browser`, `Menus` | What they say. |
| `App/Surfaces.*`, `StudioPanel.*` | Mackie Control, HUI and OSC; the browser's Studio tab (0034, 0035, 0043). |
| `App/SampleEditor`, `LauncherView`, `MetersView`, `VideoView`, `OfflineFxView` | The lower zone's sample editor, launcher, meters, video, and offline effects (0036, 0037, 0044, 0046, 0039). |
| `App/AudioEditing.cpp`, `Presets.cpp`, `ProjectFiles.cpp` | More of the Controller: audio commands, presets and A/B, file commands. |
| `App/PluginManagerView.*` | The Plug-in Manager and its window (0047). |
| `App/Keys.*`, `KeysPanel.*` | Every key as a named command the user can change (0045). |
| `App/Theme.*` | Colours, sizes, the look (docs/COLOUR.md, docs/apple-hig-review.md). |
| `Tests/` | Core tests (no JUCE); `Tests/App/` engine and window tests. |
| `tools/RenderWindow.cpp` | Draws the window into PNGs on CI (0023). |
| `Scripts/` | Example scripts. |
| `Resources/` | The app icon: `Icon.svg`, and `Icon.png` drawn from it (0048). |
| `ThirdParty/` | Lua 5.4 (MIT) and nlohmann/json (MIT). JUCE, the CLAP SDK, Signalsmith Stretch, the ARA SDK, Ableton Link and Shine (LGPL, MP3) are fetched by CMake. |

## How sound flows

    MIDI keyboard ─► Source ─► MIDI effects ─► instruments ─┐
    clips ─────────► Source ── audio clips ─────────────────┴► audio effects ─► Fader ─► folder or master ─┐
                                                                                  └─► Sends ─► returns    │
    master ─► Cue track's effects and fader (live only, never exported, 0051) ─► out  ◄──────────────────┘
       └─► a Summing track's recorder (0052)

When the project changes, the controller calls `Engine::sync`: instances
still wanted are kept, new ones made, deactivated ones dropped; the graph is
rewired only when its shape changed; each Source gets new playback lists
through `Swap`. The audio callback moves the transport, works out where the
loop wraps inside the block, and runs the graph.

## Rules that are easy to break

- No JUCE in `Source/Core`.
- The audio thread doesn't allocate or lock: data arrives whole through `Swap`.
- A gesture is one `Controller::apply` (one undo step).
- A track's devices stay MIDI effects, instruments, audio effects (`edit::normaliseChain`).
- A folder's tracks follow it in `Project::tracks` (`edit::moveTrack`).
- A plug-in's window is deleted before its instance: the engine keeps removed instances for a second and a half.
- Graph channels are set before connecting to them: a connection to a channel that doesn't exist yet is silently refused (that cost a render).
- Every colour comes from `Theme.h`; every word is 7:1.
- The graph's ends take their channel count when it's prepared: prepare it before wiring a different number of outputs (a surround render was silent until it did).
- The wiring's key holds each device node's own number, so a device replaced under the same id is rewired.
- `main` is Kalibre's own (Main.cpp): with `--kalibre-scan` it's the scanner's helper and never starts the app. Test programs that scan do the same.
- A track's `devices` are its active version's; the others' wait in their versions under the same ids, and the engine keys instances by (version, device) (0050). `Project::device` finds only the playing ones.
- The engine's "no jump" position is `noJump` (-1e9), not -1: positions below zero are real (a count-in). With -1, live playback jumped back to -1 s every block.
- What the clip launcher plays (`Track::launch`) isn't saved and survives Undo.
