# The engines, and where they came from

Copied **unchanged** from their own repositories, as Miderator does
(its decision 0003): fix them there, then copy them here again and update
this table. Embedded in the app by `cmake/EmbedLua.cmake`; run by
`Source/Core/Ideas.cpp` through the Lua 5.4 Kalibre already carries.

| Folder | Repository | Commit | Files |
| --- | --- | --- | --- |
| `good-idea/` | KallumS/Good-Idea | 91500c5 | gi_theory.lua, gi_idea.lua |
| `jsfx/` | KallumS/Midi-Evolver | 3a27075 | Midi Evolver.jsfx |
| `jsfx/` | KallumS/Transient-Shuffler | 77e0d2c | Transient Shuffler.jsfx (TransientShuffler.jsfx) |
| `jsfx/` | KallumS/Reese-Basser | b251c94 | Reese Basser.jsfx (ReeseBasser.jsfx) |
| `jsfx/` | KallumS/Reverberator | e286efc | Reverberator.jsfx |
| `jsfx/` | KallumS/Instrument-Creator | 0ca9a16 | Instrument Creator.jsfx (Instrument-Creator.jsfx) |
| `jsfx/midular/`, `jsfx/Data/midular/` | KallumS/Midular | cd0c7ca | Midular.jsfx, midular_core/engine/ui.jsfx-inc; its demo phrase, Data/midular/default.txt |
| `jsfx/midi-production-center/` | KallumS/MIDI-Production-Center | c5d1e61 | MIDI_Production_Center.jsfx, mpc_core/engine/ui.jsfx-inc, **with one change waiting to go back** (below) |
| `scripts/midular/` | KallumS/Midular | cd0c7ca | Scripts/midular_import.lua |
| `scripts/midi-production-center/` | KallumS/MIDI-Production-Center | c5d1e61 | the three loader scripts |
| `tools/starting-blocks/` | KallumS/Starting-Blocks, `plugin` branch | 02e87c1 | Starting Blocks.lua, sb_engine.lua, sb_midi.lua; `tools/imgui.lua` from its plugin/lua |
| `tools/starting-blocks-notation/` | KallumS/Starting-Blocks-Notation, `plugin` branch | 2b7de14 | Starting Blocks Notation.lua, sb_draw.lua, sb_engine.lua, sb_glyphs.lua (and its OFL notice), sb_midi.lua, sb_notate.lua |
| `tools/midi-catalogue/` | KallumS/Midi-Catalogue | 4eb91a8 | Midi Catalogue.lua, mc_catalogue.lua, mc_midi.lua, mc_orchestra.lua, mc_theory.lua |
| `tools/good-idea/` | KallumS/Good-Idea | 91500c5 | Good Idea.lua, gi_idea.lua, gi_theory.lua, gi_midi.lua (its gi_place.lua replaced by Kalibre's `tools/gi_place.lua`, decision 0093) |
| `tools/orchestration-helper/` | KallumS/Orchestration-Helper | 5b851a6 | Orchestration Helper.lua, orchestration_data.lua, orchestration_composers.lua |

The JSFX are run by ysfx (fetched by CMake, decision 0066), through a device
adapted from the wrapper the plugin branches of Reese Basser, Reverberator
and Instrument Creator share (`plugin/source/JsfxPlugin.cpp`).

The ReaScripts (`tools/`, decision 0068) are run by `Source/Engine/LuaTool/`,
adapted from the Starting Blocks plugin branch's host and Orchestration
Helper's Mac app. Their REAPER placement files are not copied: Kalibre's own
stand-ins, `tools/sb_place.lua`, `tools/mc_place.lua` and `tools/gi_place.lua`, take their place.

Midular's importer and MIDI Production Center's loaders (`scripts/`, decision
0093) are run by `Source/App/ReaperScripts.cpp`, with a `reaper` that answers
about the song in Kalibre.

**Waiting to go back to its repository**: MIDI Production Center's pad stride
was named `P_ST` and its stutter switch `p_st`. EEL2's names don't mind case
(in REAPER as in ysfx), so reading the stutter switch set the stride to 0 and
every pad was the same empty pad. Here the stride is `PAD_STRIDE`;
`patches/midi-production-center-pad-stride.patch` is the change, ready for
KallumS/MIDI-Production-Center. Copy the files again once it's there.
