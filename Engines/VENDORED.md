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
| `tools/starting-blocks/` | KallumS/Starting-Blocks, `plugin` branch | 02e87c1 | Starting Blocks.lua, sb_engine.lua, sb_midi.lua; `tools/imgui.lua` from its plugin/lua |
| `tools/starting-blocks-notation/` | KallumS/Starting-Blocks-Notation, `plugin` branch | 2b7de14 | Starting Blocks Notation.lua, sb_draw.lua, sb_engine.lua, sb_glyphs.lua (and its OFL notice), sb_midi.lua, sb_notate.lua |
| `tools/midi-catalogue/` | KallumS/Midi-Catalogue | 4eb91a8 | Midi Catalogue.lua, mc_catalogue.lua, mc_midi.lua, mc_orchestra.lua, mc_theory.lua |
| `tools/orchestration-helper/` | KallumS/Orchestration-Helper | 5b851a6 | Orchestration Helper.lua, orchestration_data.lua, orchestration_composers.lua |

The JSFX are run by ysfx (fetched by CMake, decision 0066), through a device
adapted from the wrapper the plugin branches of Reese Basser, Reverberator
and Instrument Creator share (`plugin/source/JsfxPlugin.cpp`).

The ReaScripts (`tools/`, decision 0068) are run by `Source/Engine/LuaTool/`,
adapted from the Starting Blocks plugin branch's host and Orchestration
Helper's Mac app. Their REAPER placement files are not copied: Kalibre's own
stand-ins, `tools/sb_place.lua` and `tools/mc_place.lua`, take their place.
