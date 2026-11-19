# 0066. The family's JSFX as devices

## Context

Asked on 17 November: add the user's repos as devices: Midi Evolver,
Instant Virtual Instrument, Transient Shuffler, Midi Catalogue, Starting
Blocks, Starting Blocks Notation, Orchestration Helper, Reese Basser,
Reverberator and Instrument Creator; their branches may help. Five are
REAPER JSFX (EEL2, each with its own @gfx window). Reese Basser, Reverberator
and Instrument Creator have a `plugin` branch: a JUCE wrapper that runs the
JSFX through ysfx (REAPER's EEL2 engine, open source) with its window as the
plug-in's.

## Decision

- **The JSFX themselves**, unchanged (`Engines/jsfx/`, carried in the app by
  `cmake/EmbedLua.cmake`, written to Kalibre's own folder for ysfx to load):
  Midi Evolver (a MIDI effect), Transient Shuffler and Reverberator
  (effects), Reese Basser and Instrument Creator (instruments). Nothing of
  their sound or windows is redone, so what was measured in REAPER holds.
- **Run by ysfx** (Apache-2.0, fetched by CMake at a fixed commit, its
  graphics on; its own plug-in, tests and tools off), as in the plugin
  branches.
- **One device for all five** (`Engine/JsfxDevice.*`), adapted from those
  branches' `JsfxPlugin.cpp`: one host parameter per slider (automation,
  last touched, MIDI learn), the sliders and @serialize data saved with the
  project, the @gfx window as the device's window (ysfx's graphics view),
  resizable down to the least size each asks. What differs per JSFX is a
  `JsfxSpec`, not a build of its own. MIDI the JSFX sends goes on to what
  follows, so Midi Evolver plays the instrument after it (the branch's
  wrapper dropped it).
- In Add > Kalibre, under KallumS, each with its kind.
- Midi Evolver's helper script (taking a performance into REAPER through
  shared memory) isn't needed: arming its track for MIDI (0064) records what
  it plays.

Amended 19 November (asked: make Transient Shuffler usable):

- **Drops from Kalibre's own drags.** ysfx's window already took files
  dropped from the Finder (`gfx_getdropfile`). A sound dragged from the
  browser's Sounds tab is Kalibre's own drag (`file:` and a path), so the
  device's window (`JsfxEditor`) is a drop target for those too, and hands
  the file on as a dropped one. During such a drag the mouse belongs to where
  it began, so the window is first told where it was let go: a JSFX that
  puts a file where it lands (Transient Shuffler's rows A and B) puts it
  there.
- **`export_buffer_to_project`**, a stub in ysfx, is filled in when CMake
  fetches it (`cmake/App.cmake`, as the file API's correction is): it reads
  the buffer out of the JSFX's memory and calls a hook Kalibre sets
  (`setJsfxExportHandler`). On the message thread the Controller writes it
  as a 32-bit float WAV into the song's Audio folder and puts it, as a clip
  at the cursor, on a new track under the device's own, named for the track
  and the device, one undo step (`Controller::printFromDevice`). REAPER's
  track argument (-1, the JSFX's own track) is read but a new track is made
  instead, so nothing on the device's own track is covered over.

## Consequences

Tested: the five compile, their sliders are parameters, a slider moved
survives saving and loading, each opens its own window at least its least
size; in exports, Reese Basser and Instrument Creator sound a note,
Reverberator rings on after a click, and the synth after Midi Evolver plays
what it plays from a held chord. Rendered in the rack (render 21).
Transient Shuffler: two sounds dragged as from the Sounds tab land in rows
A and B by where they're let go, its result is made, its own Print button
clicked puts the result on a new track under it, and Undo takes it away.
