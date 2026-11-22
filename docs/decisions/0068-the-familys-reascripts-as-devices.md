# 0068. The family's ReaScripts as devices

## Context

The rest of the repos asked for as devices (0066, 0067): Starting Blocks,
Starting Blocks Notation, Midi Catalogue and Orchestration Helper. All four
are REAPER Lua scripts with windows of their own: the first three drawn with
ReaImGui, Orchestration Helper with REAPER's gfx. Their repos' branches
helped (the user's pointer): Starting Blocks and its Notation each have a
`plugin` branch that already runs the window script, unchanged, as a JUCE
plug-in (an embedded Lua, a stand-in for ReaImGui on Dear ImGui, drawn with
JUCE, its block played out of MIDI out); Orchestration Helper carries a Mac
app (`macos/host.c`, `mac.m`) that does the same for gfx. Midi Catalogue has
no such branch, but is written the way Starting Blocks is.

## Decision

- **The scripts unchanged**, copied into `Engines/tools/<tool>/` and
  embedded in the app with the other engines (`cmake/EmbedLua.cmake`); a
  script's `dofile` of its own files finds them there.
- **One host for the three ReaImGui tools** (`Engine/LuaTool/`), from the
  plugin branch: `ImGuiLua.cpp` (ReaImGui's Lua API on Dear ImGui 1.91.9b,
  fetched by CMake, with the three things Midi Catalogue also uses added),
  `ToolSession` (the Lua state, `reaper`, `host`), and `LuaTool.cpp` (the
  processor, its window, the JUCE drawing of ImGui's triangles). What's
  different from the plug-in: the block reaches the audio thread through a
  `Swap`, not a lock; the tool is a `LuaToolSpec`, not a build of its own.
- **Kalibre's own place stand-ins** (`Engines/tools/sb_place.lua`,
  `mc_place.lua`), the same functions as the REAPER ones: Audition and Play
  with the Song through the device's MIDI out, to the instrument after it;
  **Insert at Cursor** puts the block on the device's track at the playhead
  as a MIDI clip, and Midi Catalogue's block across a section as a new track
  per instrument, named for it, under the device's track, all one undo step
  (`Controller::placeToolBlock`). Export MIDI File writes to Music/<tool>.
  No dragging out: the clip is put there directly.
- **Orchestration Helper through a port of its Mac host** (`GfxTool.cpp`):
  the same gfx and reaper calls with the same meanings, one per device
  instead of one per program, drawn into a picture with JUCE, its menus
  JUCE's. A reference beside the track: everything passes through it.
- **The window is Kalibre's**: a script that would close itself (Esc in Midi
  Catalogue, the close box) keeps running; its settings (REAPER's ExtState)
  are saved with the project, and a loaded project starts the script again
  so it reads them, as REAPER does.
- **Lua is built as C++** (as the plugin branch builds it), so an error
  raised inside ImGui unwinds cleanly; Kalibre's own Lua code includes
  Lua's headers without `extern "C"`.
- Add > Kalibre: Starting Blocks, Starting Blocks Notation, Midi Catalogue,
  Orchestration Helper, each a MIDI effect.

## Consequences

Tested: each script starts and draws without an error; the three play their
block through MIDI out; settings come back from the saved state; Insert at
Cursor (through the real stand-ins) makes the clip at the playhead with the
notes in place, and Midi Catalogue's parts a track each, both undone in one
step. Rendered in the rack (renders 23), and Orchestration Helper with
"trombone" typed into its search box.

Some words in the scripts are REAPER's still, because the scripts are
unchanged: Midi Catalogue's Audition tip speaks of the virtual keyboard,
its Export tip of REAPER's resource folder, "the edit cursor" means the
playhead. Changing them belongs in their own repos.

## Amended 22 November (0111)

`ImGuiLua.cpp` has what DAW Battle Pass also uses: child windows, disabled
items, text input, the clipboard, the foreground draw list, paths, clip
rectangles, more shapes, and bold fonts. A tool can add to its `reaper`
(`LuaToolSpec::extendReaper`).
