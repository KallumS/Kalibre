# Updating the family's engines

Kalibre carries fifteen of your own projects inside it: JSFX, ReaScripts, two
JUCE plug-ins and a port of Trombolese. Each is a copy of one commit of its
repository. When a repository moves on, Kalibre doesn't follow by itself: the
copy has to be brought in, checked, built and tested. This page says how, and
what can need more than a copy.

## The short version

Ask Claude: *"Update Midular in Kalibre"*, or *"Update all the engines"*. That
runs the tool below, makes whatever changes it points to, runs the tests, and
pushes, so the Mac build has it.

## The tool

```sh
python3 tools/update_engines.py                  # which have new commits
python3 tools/update_engines.py check Midular    # what an update would bring, nothing changed
python3 tools/update_engines.py update Midular   # bring it in
python3 tools/update_engines.py update all       # every engine with new commits
python3 tools/update_engines.py update "MIDI Production Center" --ref main   # from another branch
```

It reads `Engines/engines.json`: for each engine, its repository and branch,
the exact commit Kalibre has, which file there becomes which file here (some
go to two places), and what to watch. An update copies the files, records the
new commit there and in `Engines/VENDORED.md`, and prints a **LOOK** for each
thing a copy can't take care of. Then build and test as in `CLAUDE.md`, deal
with each LOOK, and commit.

Its checks:

| It looks for | Why |
| --- | --- |
| A JSFX slider gone, renumbered or renamed | Songs, automation and MIDI learn hold sliders by number |
| Two spellings of one name in a JSFX (`P_ST`, `p_st`) | EEL2 ignores case: they're one variable. MIDI Production Center's pads were all empty because of it (decision 0093) |
| A `reaper.` or `ImGui.` call Kalibre doesn't answer | The scripts run on Kalibre's stand-ins for REAPER and ReaImGui; a missing one stops the script |
| A REAPER-facing file changed that Kalibre replaces with its own (`gi_place.lua`, `sb_place.lua`, `mc_place.lua`) | Kalibre's version must make the same change |
| The plug-in host Kalibre's was adapted from changed (Starting Blocks' plugin branch, Orchestration Helper's Mac app) | The same fix may be wanted here |
| A new file beside the ones carried (a new import, module or source file) | It may need carrying, or adding to the build |
| A file gone from the repository | Kalibre keeps its old copy until told otherwise |
| Trombolese's model changed | It's a port, not a copy: see below |

## By kind

**JSFX** (Midi Evolver, Transient Shuffler, Reese Basser, Reverberator,
Instrument Creator, Midular, MIDI Production Center). Usually a copy. New
sliders at the end are safe; moving or removing one changes what saved songs
mean. Midular's importer and MIDI Production Center's loaders are REAPER
scripts run by `Source/App/ReaperScripts.cpp`, which answers about sixty
REAPER functions; a new one is a few lines there.

**ReaImGui windows** (Starting Blocks, Starting Blocks Notation, Midi
Catalogue, Good Idea) and **Orchestration Helper** (gfx). The window scripts
are copied unchanged; their placement files are Kalibre's own (`Engines/tools/
*_place.lua`). A new ReaImGui call goes into `Source/Engine/LuaTool/
ImGuiLua.cpp`. Good Idea's engine is also used by the pattern editors' Random
buttons (`Engines/good-idea/`): the tool updates both copies. Midi Suggester's
engine (`Engines/midi-suggester/`, kind `lua-engine`, decision 0100) is run by
`Core/Ideas` for the top bar's Random: after updating it, run the core tests
(`ideas: Midi Suggester's engine...` and `random song: ...`).

**JUCE plug-ins** (Instant Virtual Instrument, ScaleView). Their source is
compiled into Kalibre. Instant Virtual Instrument's whole `Source` folder is
synced and picked up by the build; ScaleView's files are listed in
`cmake/App.cmake`, so a new one is added there. ScaleView's `ScaleModel.h` is
also Kalibre's own chord reader (`Source/Core/ScaleModel.h`): both copies move
together, or the two halves of the app read chords differently.

**Trombolese** is the exception: Kalibre runs a C++ translation of its Python
model (`Source/Core/Trombolese.cpp`), checked against the Python's own renders
sample by sample. When its model changes:

1. Only numbers (tuning, geometry, the reed's settings): run
   `tools/trombolese_reference.py` with a checkout of the new version, put its
   tables in `Source/Core/TromboleseTables.inc` and its renders in
   `Tests/fixtures/trombolese/`, then remake the note table with
   `tools/TromboleseCalibrate.cpp` (about fifteen minutes). The tests then
   show whether the port still matches.
2. The physics (the waveguide, the reed, the geometry's shape): make the same
   change in `Trombolese.cpp`, then step 1. The test compares the first 50 ms
   sample by sample and fails until the C++ does exactly what the Python does.

## When a fix belongs to the repository

A bug in an engine is fixed in its own repository first, then copied in
(`CLAUDE.md`'s rule). Until it's merged there, Kalibre can take it from the
fix's branch: set `ref` in `engines.json` to the branch (MIDI Production
Center does now, `fix-pad-stride-case`), and back to `main` once merged.
