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

The JSFX are run by ysfx (fetched by CMake, decision 0066), through a device
adapted from the wrapper the plugin branches of Reese Basser, Reverberator
and Instrument Creator share (`plugin/source/JsfxPlugin.cpp`).
