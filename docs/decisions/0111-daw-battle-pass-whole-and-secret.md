# 0111. DAW Battle Pass, the whole of it, a secret behind the timer

## Context

Asked (22 November): the top bar's timer was adapted from the musician's DAW
Battle Pass (KallumS/DAW-Battle-Pass, 0091); now the entirety of it, the timer,
rewards and everything else, as a secret: the only way to open it is to
left-click the Making Music timer seven times in a row.

The battle pass is a REAPER Lua script on ReaImGui: a window
(`DAW Battle Pass.lua`, eight pages, celebrations, effects), four pure Lua
engines (`bp_game`, `bp_loot`, `bp_theory`, `bp_fx`) and one file that talks
to REAPER, `bp_watch.lua`, only through `reaper`. Kalibre already runs the
family's ReaImGui scripts unchanged on Dear ImGui (0068). Its `ableton` branch
(0019 there) shows a smaller panel; the musician asked for everything.

## Decision

- **The script unchanged**: its six files in `Engines/tools/daw-battle-pass/`,
  in `engines.json` (kind `lua-window`), embedded with the other engines,
  `bp_watch.lua` included.
- **A `reaper` answered from Kalibre's song** (`App/BattlePass.cpp`), added
  to the tool's `reaper` (`LuaToolSpec::extendReaper`): the change count is
  the Controller's undo steps (`changeCount`); the play state, the cursor,
  the mouse; js_ReaScriptAPI's foreground window is Kalibre when it's the app
  in front, and its keyboard anything Kalibre counts as someone there
  (`activityCount`: keys, clicks, MIDI played in, the mouse); tracks, the
  master, clips (items and takes), their notes, devices (named as REAPER
  names plug-ins, "VST3i: Serum (Xfer Records)"), markers and sections
  (regions); every plug-in known and Kalibre's own instruments as what's
  installed. Rewards into the song: a block between `Undo_BeginBlock` and
  `Undo_EndBlock` is made on a copy and applied as one undo step (an idea: a
  MIDI clip at the cursor on the chosen track, or a new one); regions become
  sections; `SetCurrentBPM` the song's tempo. Starting with REAPER returns
  "couldn't" (`AddRemoveReaScript` 0), and nothing is written.
- **ReaImGui**: what the battle pass uses that the others didn't, added to
  `ImGuiLua.cpp` (0068 amended): child windows, disabled items, window
  position and size, the cursor's screen position, item focus, text input,
  the clipboard, the foreground draw list, rectangles, multicoloured
  rectangles, triangles, quads, paths and arcs, clip rectangles, and bold
  fonts (a bold face of its own, keyed with the size).
- **Always running, seen only by the secret**: started three seconds after
  Kalibre opens (making its fonts takes a moment), it runs a frame four times
  a second while its window is shut, so music made with it shut still counts
  and its rewards wait; its window (30 frames a second) is opened only by
  seven left clicks on the timer, each within 1.5 s of the last
  (`SecretClicks`). Nothing in a menu or tip mentions it. A window of its
  own (0002 amended: not music); ⌘W or its close button shuts it; Esc is
  the game's.
- **Progress** in Kalibre's folder as REAPER's resource path:
  `Application Support/Kalibre/Data/DAW Battle Pass/progress.lua`. The first
  time, a save from REAPER's copy (`Application Support/REAPER/Data/...`) is
  copied over, never over Kalibre's own.

## Consequences

- Its words are REAPER's ("something happened in REAPER", "Start the Battle
  Pass When REAPER Starts"): the scripts are copied unchanged, so a wording
  for Kalibre goes to the battle pass's repository first.
- Two clocks: the top bar's (per song, 0091) and the battle pass's own (the
  musician's, across songs).
- Tested: the script runs with every page drawn and no call missing; a new
  track and its notes are seen and earn XP; an idea lands at the cursor as one
  undo step, regions as sections, a tempo; REAPER's save is brought over
  once; seven clicks open it, a pause starts the count again. Rendered: the
  welcome and the eight pages (45, 46).
- Not tried on the Mac: how fast its window draws there (a Debug build on
  Linux draws a frame slowly; the Mac's release build draws the other tools
  smoothly), and Retina fonts at 200%.
