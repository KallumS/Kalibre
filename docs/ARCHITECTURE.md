# Architecture

## The shape of it

| | |
| --- | --- |
| `Source/Core/` | The music, with **no JUCE** (decision 0001): tests run in seconds. |
| `Core/TempoMap.*` | Ticks (960 a quarter) to seconds and back through held and ramped tempos; bars through time signatures (0004). |
| `Core/Model.*` | The project as plain data: tracks (folders, returns, the master), versions of clips, MIDI and audio clips, devices, sends, automation, modulators, articulation maps (0003, 0005, 0006). |
| `Core/Edit.*` | Every change as a function, and the undo history (0008). |
| `Core/Roll.*` | The piano roll's mouse rules as a state machine (0009). |
| `Core/Patterns.*` | Drum grid, step sequencer and Euclidean rhythms, read from and written to notes, each only its own (`Note::from`, 0014, 0059). |
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
| `Core/AudioTools.*` | Peaks, silence, transients (as candidates a sensitivity chooses from, for the Sampler's slices), audio quantise markers, sample edits, FFT, YIN pitch; a sample's tempo and note (0036, 0044, 0095). |
| `Core/Song.*` | Time edits, sections, ripple, linked clips, track groups (0037; the clip launcher taken out, 0098). |
| `Core/Surround.*` | Speaker gains for quad, 5.1 and 7.1 (0040). |
| `Core/ProjectTools.*` | A project's files, relinking, templates, ID3 tags, Final Cut XML (0041). |
| `Core/Mpe.*` | Note expression and MPE playback (0042). |
| `Core/Toolbox.*` | The tools Control-right-click chooses, each area's, their modes; Split into pieces, Glue, clearing a range, Line's shapes, Time Warp (0092). |
| `Core/Trombolese.*` | Trombolese's voice, ported from its Python (waveguide, reed, geometry), its tables made by its own functions, and each note's partial and slide (0093). |
| `Core/RandomSong.*` | The Random button's song: parts, instruments, effects and loops chosen by chance, music from Ideas (0101). |
| `Core/Mcp.*` | The Model Context Protocol: JSON-RPC in, replies out, tools by name (0099). |
| `Core/Library.*` | The Sounds browser's list: what was scanned, the favourites and folders, in folders or one list, A to Z or shuffled; tempo from a file's name (0094). |
| `Core/WorkClock.*` | The top bar's timer: DAW Battle Pass's active-time clock (0091). |
| `Core/NoteFixer.*` | Note Position & Volume Fixer's engine: where each note is heard in a render, and how loud (0085). |
| `Core/NoteSplit.*` | The Note Splitter's rule: a chord's notes, top to bottom, an output each (0083). |
| `Core/Ideas.*` | Good Idea's engine (Lua, `Engines/good-idea/`, embedded by `cmake/EmbedLua.cmake`): drum grooves and motifs for the Random buttons (0062); Midi Suggester's (`Engines/midi-suggester/`): chords and a bass under a melody, a melody over chords (0100). |
| `Core/Controllers.*` | The MIDI controllers' standard names (0060). |
| `Core/AutoCc.*` | AutoCC's engine, ported from the JSFX: a clip's notes to its CC curves; instrument names to its families (0060). |
| `Core/Hui.*` | The HUI protocol (0043). |
| `Core/Xml.*`, `Core/MusicXml.*` | A small XML reader and writer (Miderator's), and MusicXML in and out (0057). |
| `Core/ProjectFile.*` | `.kalibre` files: JSON with nlohmann/json; the articulation library. |
| `Core/Articulations.*` | Cubase, Logic and Reaticulate articulation maps read (0072). |
| `Core/NoteSelect.*` | Notes chosen by chord degree, beat and line; Explode Chords (0075). |
| `Core/StepInput.*` | Step input's keys and lengths (0076). |
| `Core/DawProject.*` | DAWproject's XML, in and out; the zip is the window's (0077). |
| `Source/Script/LuaHost.*` | The `kalibre` Lua API (0019). |
| `Source/Engine/` | JUCE's audio side. |
| `Engine/Engine.*` | The audio graph built from the project, plug-in instances kept across changes, the transport and loop, rendering (0016). |
| `Engine/TrackProcessors.*` | Each track's Source (clips, live input, recording, automation, modulators), Fader and Sends; the metronome. |
| `Engine/ClapFormat.*`, `ClapGuiMac.mm` | Hosting CLAP (0021). |
| `Engine/PluginManager.*` | Scanning (each plug-in in a helper: Kalibre run with `--kalibre-scan`), turning plug-ins off and on, and making them (0047). The helper's `controls` mode reads every control for the Random Settings Report (0104). |
| `Engine/MorphProcessor.*` | The X/Y morph (0018), and the latency and gain devices the tests use. |
| `Engine/ContainerProcessor.*` | The Container: plug-ins in a row, each on a band, the mid or the sides (0055). |
| `Core/DrumSounds.*` | Whether a sound is a drum, and which: name, folders, length, the hit's shape (0106). |
| `Engine/DrumMachine.*`, `DrumMachineEditor.cpp`; `Core/DrumKit.*` | Kalibre Drums: pads of samples or hosted plug-ins, its own steps from the playhead; the kit as JSON, Random Samples, the pattern as notes (0105). |
| `Engine/AraHost.*` | ARA documents for plug-ins like Melodyne (0028). |
| `Engine/SyncDevices.*` | Clock and timecode out, clock in, Ableton Link (0034). |
| `Engine/Warp.*` | Audio in memory, and warping (0022); what no clip asked for at the last sync is let go of (0103). |
| `Engine/ParallelGraph.*` | The graph played on every performance core: a plan made after each rebuild, nodes taken by the audio thread and workers as their inputs finish (0078). |
| `Engine/AudioFiles.*` | 32-bit float WAV, the one format Kalibre keeps audio in (0079). |
| `Engine/Swap.h` | Handing new data to the audio thread without locks. |
| `Engine/Editors.h` | Opening and closing plug-ins' windows, telling the plug-in first (0056). |
| `Engine/LoudnessMatch.h` | Match Loudness: output held to the input's short-term loudness (0056). |
| `Engine/CrashContext.h` | The plug-in the audio thread is in, for the crash report (0073). |
| `Engine/TimedPlugin.h` | Wraps every plug-in in the graph to time it; `Engine::instance` hands out the plug-in inside (0038). On a track armed for MIDI catches what its instrument is played (`MidiTap`, 0064). |
| `Engine/BuiltinInstruments.*`, `HardwareInsert.*` | Kalibre Synth and Sampler (0039); the hardware insert and the interface's channels it shares (0040). |
| `Engine/JsfxDevice.*` | The family's JSFX as devices, run by ysfx (0066): Kalibre's own file drags taken as dropped files; `export_buffer_to_project` (filled in when CMake fetches ysfx) handed to the Controller, which prints it onto a new track (0066 amended). |
| `Engine/NoteSplitter.*` | The Note Splitter device, and its outputs' nodes into other tracks' Sources (0083). |
| `Engine/PanLaw.h` | Constant power, sine curves, exactly 0 dB in the middle (0081). |
| `Engine/ScaleViewDevice.cpp` | ScaleView's own processor in a HostedProcessor, passing the notes on (0093). |
| `Engine/HostedProcessor.*` | A plain JUCE `AudioProcessor` compiled into Kalibre as a device: parameters passed on, its window inside the device's (0067). |
| `Engine/LuaTool/` | The family's ReaScripts as devices (0068): `ImGuiLua` (ReaImGui on Dear ImGui), `ToolSession` (a script's Lua, `reaper` and `host`), `LuaTool` (the processor, the window); `GfxTool` (gfx, for Orchestration Helper). |
| `Engine/Mp3Writer.*` | MP3 export through Shine (0041). |
| `Engine/NetworkMidi*` | The Mac's network MIDI session (0043). |
| `Source/App/` | The window (0002). `Controller` owns the project, undo, the selection and every command; the views ask it. |
| `App/TopBar` (and `TransportBar`, along the bottom, 0095), `Arrange`, `LowerZone`, `PianoRoll`, `PatternEditors`, `NotationView`, `DeviceRack`, `Mixer`, `Browser`, `Menus` | What they say. |
| `App/Surfaces.*`, `StudioPanel.*` | Mackie Control, HUI and OSC; the Studio window's settings (0034, 0035, 0043). |
| `App/SettingsWindows.*` | The Studio window (File > Studio) and the Keys window (Help > Keys…), settings windows of their own (0110). |
| `App/FilePanels*` | The Mac's Open and Save panels placed near the top of the screen, in front of plug-ins' windows (0110). |
| `App/SampleEditor`, `MetersView`, `VideoView`, `OfflineFxView` | The lower zone's sample editor, meters, video, and offline effects (0036, 0044, 0046, 0039). |
| `App/AudioEditing.cpp`, `Presets.cpp`, `ProjectFiles.cpp`, `NoteFixing.cpp` | More of the Controller: audio commands, presets and A/B, file commands, Fix Note Timings and Volume (0085). |
| `App/LibraryView.*`, `LibraryScanner.*`, `PreviewPlayer.*`, `LibraryFolders.*` | The browser's Sounds tab: the list, the scan of the chosen folders on its own thread, previews made off the audio thread (stretched to the song, MIDI through Kalibre Synth) and handed over through `Swap`, Kalibre > Library Folders… (0094). |
| `App/RandomSongs.cpp` | The Random button's catalogue (plug-ins, Kalibre's own, the Sounds tab's sounds), its Samplers loaded, the song put in place (0101). |
| `App/AgentTools.*`, `McpServer.*` | What AI agents can do (the tools, on the message thread, each an undo step), and Kalibre listening for them on this Mac; `Kalibre --mcp`, the stdio bridge (0099). |
| `App/ToolPicker.*`, `ArrangeTools.cpp`, `PianoRollTools.cpp` | The toolbox at the pointer, its icons and pointers; what each tool does in the arrangement and the piano roll (0092). |
| `App/ReaperScripts.*` | The family's REAPER scripts that hand a JSFX its phrase (Midular's importer, MIDI Production Center's loaders), run unchanged with a `reaper` for this song (0093). |
| `App/NoteOptions.h` | Velocity, note chance, random velocity range and random timing: the menu shared by the piano roll, steps and drums (0061). |
| `App/CcLanes.*` | The CC lanes under the piano roll and notation (0060). |
| `App/PluginClip.*` | A plug-in's window clipped to what the rack shows and scaled to fit: a native view on the Mac; sideways swipes over the rack (0058). |
| `App/NoteSplitterEditor.*` | The Note Splitter's panel: its outputs and their tracks (0083). |
| `App/PluginManagerView.*` | The Plug-in Manager and its window (0047); File As (0069). |
| `App/ArticulationManagerView.*` | The Articulation Manager and its window (0072). |
| `App/CrashReports.*` | The crash handler and its reports (0073). |
| `App/BattlePass.*` | DAW Battle Pass (0111): its script, unchanged in `Engines/tools/daw-battle-pass/`, run as a Lua tool that's never a device, with `reaper` answered from the song; unseen until the timer is clicked seven times (`SecretClicks`). |
| `App/Keys.*`, `KeysPanel.*` | Every key as a named command the user can change (0045), in the Keys window. |
| `App/Theme.*` | Colours, sizes, the look (docs/COLOUR.md, docs/apple-hig-review.md). |
| `Tests/` | Core tests (no JUCE); `Tests/App/` engine and window tests. |
| `tools/RenderWindow.cpp` | Draws the window into PNGs on CI (0023). |
| `tools/export-chat.py` | Writes a session's transcript as the chat backup in `docs/sessions/`; a new part's title and description are its last two arguments. |
| `Engines/` | The family's engines, JSFX (with their imports and data) and ReaScripts (`tools/`, `scripts/`), copied unchanged from their repositories (`engines.json`, `VENDORED.md`), embedded in the app. `tools/update_engines.py` brings them up to date (docs/UPDATING-ENGINES.md). |
| `Scripts/` | Example scripts. |
| `Resources/` | The app icon: `Icon.svg`, and `Icon.png` drawn from it (0048). |
| `ThirdParty/` | Lua 5.4 (MIT), nlohmann/json (MIT) Instant Virtual Instrument's and ScaleView's source (GPL-3.0, 0067, 0093). JUCE, the CLAP SDK, Signalsmith Stretch, the ARA SDK, Ableton Link, Shine (LGPL, MP3), ysfx (Apache-2.0, 0066) and Dear ImGui (MIT, 0068) are fetched by CMake. |

## How sound flows

    MIDI keyboard ─► Source ─► MIDI effects ─► instruments ─┐
    clips ─────────► Source ── audio clips ─────────────────┴► audio effects ─► Fader ─► folder or master ─┐
                                                                                  └─► Sends ─► returns    │
    master ─► Cue track's effects and fader (live only, never exported, 0051) ─► out  ◄──────────────────┘
       └─► a Summing track's recorder (0052)
    MIDI effects ─► Note Splitter ─► its outputs ─► other tracks' Sources, as MIDI played in (0083)

Processing is 32-bit float; every ─┴► and ─┐ above, where signals meet, is
summed at 64 bits and rounded once (0081).

When the project changes, the controller calls `Engine::sync`: instances
still wanted are kept, new ones made, deactivated ones dropped; the graph is
rewired only when its shape changed; each Source gets new playback lists
through `Swap`. The audio callback moves the transport, works out where the
loop wraps inside the block, and runs the graph: its nodes on every
performance core, each as soon as what feeds it is done (0078).

## Rules that are easy to break

- No JUCE in `Source/Core`.
- The audio thread doesn't allocate or lock: data arrives whole through `Swap`.
- Where signals meet, they are added at 64 bits and rounded to float once
  (0081): the plan's mix points, a track's clips, the Container. Anything new
  that mixes does the same; a neutrality test (`TestNeutral.cpp`) says so.
- A bypassed plug-in is never called: `TimedPlugin` passes the sound on,
  delayed by its latency (0081). Its own bypass parameter isn't used. A
  switched-off one is stood in for by a `HeldDelay` as long (0086).
- A lane is read for the sound reaching what it moves (0096): the Source
  keeps where the song was (`spans`), and each fader, send and plug-in has
  an `automationDelay`, set by `Engine::lineUpAutomation` after every graph
  rebuild from the graph's own latency count. Anything new that reads a lane
  for a processor after the Source uses `ticksFor` with its delay.
- The loop wraps on the first sample at or after its end (0096), never by
  rounding down.
- A song replaced (Open, New, a template, a DAWproject, Random) calls
  `Engine::newSong` first: devices are numbered from 1 in every song, so an
  instance kept by its number played the next song with the last one's
  settings (0101).
- Automation is drawn sample by sample by the track's Source, which plays
  before everything it moves (0089). A plug-in's block is cut into pieces at
  its lanes' points by `TimedPlugin`; anything that adds automation reads the
  Source's ticks (`ticksFor`, with its delay), never once a block.
- At most 1000 tracks, checked in `Controller::apply` (0088): a change that
  adds tracks some other way (a script, opening a file) checks too.
- A gesture is one `Controller::apply` (one undo step). A tool's drag that shows its change as it goes ends in `Controller::applyLive` (0092).
- A tool other than Select takes only the left click; right-clicks keep their menus, and Control-right-click is the toolbox's (0092).
- A track's devices stay MIDI effects, instruments, audio effects (`edit::normaliseChain`).
- A folder's tracks follow it in `Project::tracks` (`edit::moveTrack`).
- A plug-in's window is deleted before its instance: the engine keeps a removed node at least a second and a half, and deletes it (`Engine::letGoOfOld`) only once it holds the last reference (JUCE's graph and the plan have let go) and no window of it is open (0110). Never let JUCE's graph be the one to delete a plug-in.
- A plug-in's window in the rack is opened only once its panel is in a window on the screen (`DeviceRack::pluginsNeedAWindow`, on the Mac): a plug-in given a native view with no window set an impossible size and AppKit stopped Kalibre (0110).
- Anything drawn over the plug-ins' windows must be its own window or go through `ui::present`, which hides them meanwhile (those in their own windows too) and puts the question near the top, under the top bar (0110): on the Mac they're native views, drawn above everything Kalibre draws (0056).
- On the Mac nothing Kalibre draws clips a plug-in's window, and a `Viewport` doesn't either: a plug-in in the rack is shown through a `PluginClip` (0058).
- A plug-in's window is opened and closed with `editors::open` and `editors::close`, never `delete`: JUCE 9 needs the plug-in told first, or it won't open another (0056).
- Graph channels are set before connecting to them: a connection to a channel that doesn't exist yet is silently refused (that cost a render).
- Every colour comes from `Theme.h`; every word is 7:1.
- The graph's ends take their channel count when it's prepared: prepare it before wiring a different number of outputs (a surround render was silent until it did).
- The wiring's key holds each device node's own number, so a device replaced under the same id is rewired.
- `main` is Kalibre's own (Main.cpp): with `--kalibre-scan` it's the scanner's helper and never starts the app. Test programs that scan do the same.
- A track's `devices` are its active version's; the others' wait in their versions under the same ids, as settings only: the engine makes instances for the playing version alone, keyed by (version, device) (0050, 0103). `Project::device` finds only the playing ones.
- Song versions (0103): the playing one is the `Project`; the other is `otherVersion`, its saved JSON. Their devices share numbers, so a change of `songId` makes the Controller call `Engine::newSong` before syncing. `Controller::storeDeviceStates` before anything that stores or switches.
- The engine's "no jump" position is `noJump` (-1e9), not -1: positions below zero are real (a count-in). With -1, live playback jumped back to -1 s every block.
- A note's chance, velocity range and timing are rolled on the audio thread as each note-on comes up (`playback::roll`); a late one waits in `TrackSource::waiting`, a fixed list, emptied on a jump or stop (0061).
- `ZoneTab`'s numbers are saved in workspaces: 3 (the clip launcher, taken out, 0098) isn't used again.
- A looped clip's content is its loop, from 0 (`patterns::windowOf`): editors write there, not from the offset. Loop Its Content (`edit::loopContent`) moves a MIDI clip's content to offset 0 first.
- The arrangement snaps with `arrangeSnap` (the top bar's Grid), the editors with `snapTick` (the lower zone's) (0070).
- A plug-in's role is `PluginManager::roleOf` (the user's filing, what the scan learned, then the description), never the static `roleFor` alone (0069).
- Nothing important along the bottom but the transport (0095, asked for): the message line stays at the top.
- Kalibre Sampler's slices are found on the audio thread from `candidates`, made when the sample loads: never call `transients` there (it allocates).
- A number shown with no decimals goes through `ui::number`: juce::String's own takes 0 decimals to mean all of them ("502.246 Hz", 0109).
- `paint` changes nothing it's drawn from: the knob panels cut their group boxes and the headings walked down a step each repaint (0109).
- The window (`MainWindow.h`) passes keys that reach it with nothing focused on to `MainComponent::keyPressed`: after a dialog closes, the shortcuts still work (0109).
- An export of the whole song ends at `Project::songEnd` (the bar after the last clip); `endTick` is the view's, at least 16 bars (0109).
- A message the engine sends itself (`MessageManager::callAsync`) carries `std::weak_ptr<bool> (aliveToken)` and does nothing once it's expired; the Controller clears every engine callback it set (`onDevicePluginChanged`, `onLearned`...) in its destructor. A message can arrive after either has gone: on the Mac a plug-in's "settings changed" arriving after its engine crashed the tests (0108).
- The crash handler may only use calls safe in a signal handler: no allocation, no JUCE (0073).
- DAW Battle Pass's files are the family's, unchanged (0111): what Kalibre needs goes in `App/BattlePass.cpp`'s `reaper` or in `ImGuiLua.cpp`. Tests and renders never start it (`MainComponent::startBattlePass` is the app's), and set `BattlePass::resourceFolder` so nothing touches the real save.
- Audio Kalibre writes for itself goes through `audiofile::floatWav` (0079); audio brought in that isn't 32-bit float WAV is copied as one.
- Rendering one track (`Engine::renderTrack`, freeze and bounce, 0080) wires only it out; an offline plan leaves out what can't reach the output.
- The graph's nodes play on several threads at once (0078): a processor may not share state with another node unless it's marked `runsAlone`; anything per thread is `thread_local`. Rebuild the graph with `graph.rebuild()` on the message thread, so a plan is made.
- Lua is compiled as C++ (0068): include its headers plainly, never inside `extern "C"`, or nothing links.
- The family's scripts in `Engines/` are copied unchanged: what Kalibre needs differently goes in its own stand-ins (`Engines/tools/sb_place.lua`, `mc_place.lua`, `gi_place.lua`) or the host, and a fix to a script goes to its repo first.
- EEL2's names ignore case: `P_ST` and `p_st` are one variable (0093). `tools/update_engines.py` looks for two spellings in a JSFX.
- A new JSFX isn't run through `@serialize` until a song gives it a state, as in REAPER; ysfx's `file_avail` there is mended to count what's left (cmake/App.cmake).
- On the Mac's disk names ignore case too: no two files may differ only by case (`Core/Toolbox.h` and `App/ToolPicker.h`, once `ToolBox.h`, 0092).
