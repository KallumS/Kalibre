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
| `Core/ProjectFile.*` | `.kalibre` files: JSON with nlohmann/json. |
| `Source/Script/LuaHost.*` | The `kalibre` Lua API (0019). |
| `Source/Engine/` | JUCE's audio side. |
| `Engine/Engine.*` | The audio graph built from the project, plug-in instances kept across changes, the transport and loop, rendering (0016). |
| `Engine/TrackProcessors.*` | Each track's Source (clips, live input, recording, automation, modulators), Fader and Sends; the metronome. |
| `Engine/ClapFormat.*`, `ClapGuiMac.mm` | Hosting CLAP (0021). |
| `Engine/PluginManager.*` | Scanning and making plug-ins of every format. |
| `Engine/MorphProcessor.*` | The X/Y morph (0018), and a latency device the tests use. |
| `Engine/AraHost.*` | ARA documents for plug-ins like Melodyne (0028). |
| `Engine/Warp.*` | Audio in memory, and warping (0022). |
| `Engine/Swap.h` | Handing new data to the audio thread without locks. |
| `Source/App/` | The window (0002). `Controller` owns the project, undo, the selection and every command; the views ask it. |
| `App/TopBar`, `Arrange`, `LowerZone`, `PianoRoll`, `PatternEditors`, `NotationView`, `DeviceRack`, `Mixer`, `Browser`, `Menus` | What they say. |
| `App/Theme.*` | Colours, sizes, the look (docs/COLOUR.md, docs/apple-hig-review.md). |
| `Tests/` | Core tests (no JUCE); `Tests/App/` engine and window tests. |
| `tools/RenderWindow.cpp` | Draws the window into PNGs on CI (0023). |
| `Scripts/` | Example scripts. |
| `ThirdParty/` | Lua 5.4 (MIT) and nlohmann/json (MIT). JUCE, the CLAP SDK and Signalsmith Stretch are fetched by CMake. |

## How sound flows

    MIDI keyboard ─► Source ─► MIDI effects ─► instruments ─┐
    clips ─────────► Source ── audio clips ─────────────────┴► audio effects ─► Fader ─► folder or master ─► out
                                                                                  └─► Sends ─► returns

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
