# 0093. Five more of the family's devices: Midular, MIDI Production Center, Good Idea, ScaleView, Trombolese

## Context

Asked (19 November): add Good Idea, Trombolese, Midular, ScaleView and MIDI
Production Center as devices. Each is a different kind of thing: Midular and
MIDI Production Center are JSFX, each handed its MIDI phrase by a REAPER
script through shared memory (gmem); Good Idea is a ReaImGui window script;
ScaleView is a JUCE plug-in; Trombolese is a physical model in Python
(NumPy), not yet a realtime instrument anywhere.

## Decision

- **Midular and MIDI Production Center** run in the JSFX device (0066), each
  from a folder with its imports, Midular with its demo phrase in a `Data`
  folder as REAPER's resource path has it. Their **own scripts run
  unchanged**: Midular's importer and MIDI Production Center's two loaders,
  in `Source/App/ReaperScripts.cpp`, a Lua each with a `reaper` that answers
  about sixty REAPER functions about this song. Its tracks, devices and
  parameters (a JSFX's in its sliders' own units), the chosen MIDI clips as
  the selected items, their notes, the tempo map, markers, `gmem` (ysfx's
  shared memory, written with EEL2's own allocator), `defer`, and messages
  into the message line. A device's menu has **Load the Chosen MIDI Clip**.
  MIDI Production Center's background loader runs while the song has one, so
  its own **Load Item** button works.
- **Three corrections found on the way**:
  - ysfx's `file_avail` in `@serialize` said 1 instead of how many values
    were left (REAPER's), so Midular never read its saved phrase back; it's
    mended where CMake fetches ysfx.
  - A new JSFX was run once through `@serialize`, save then load. REAPER
    doesn't do that, and it left MIDI Production Center's demo off its pads,
    so it's gone.
  - **EEL2's names ignore case**, in REAPER too. MIDI Production Center's pad
    stride `P_ST` was its stutter switch `p_st`, so every pad was one empty
    pad (very likely why its first REAPER try played nothing). Two functions
    also mixed a region's `S, E` with a note's `s, e`. All three were fixed in
    its own repository (branch `fix-pad-stride-case`, its ADR 0010), whose
    tests now fold case as REAPER does and fail on two spellings of a name.
    Kalibre carries that branch until it's merged.
- **Good Idea** runs as Starting Blocks does (0068): its window script
  unchanged; its placement file is Kalibre's own (`Engines/tools/
  gi_place.lua`). An idea goes on the device's track at the playhead, or on
  tracks of its own when laid out that way. Audition plays through MIDI out,
  each part on its channel (drums on 10: tool blocks now keep their channel).
  Play with REAPER is the device's Play with the Song. ReaImGui gained
  `InputInt`, `SetNextItemWidth` and `GetMousePos`.
- **ScaleView** is compiled in as Instant Virtual Instrument is (0067): its
  source unchanged, its `createPluginFilter` renamed at compile time so the
  two don't meet. It's a MIDI effect before the instrument, where the notes
  are: it shows the scale and names the chord, and passes the notes on (the
  wrapper says so for it, as the plug-in itself sends no MIDI). The sound is
  unchanged sample for sample.
- **Trombolese** is a port. Its Python voice (the Kelly-Lochbaum waveguide,
  the two-valve reed, the geometry) is translated operation for operation
  into `Source/Core/Trombolese.cpp`. It matches the Python's own renders to
  within 1e-13, sample by sample, as its rule asks ("compiling is not
  evidence"). What the Python works out offline comes from its own functions,
  as tables: the bore lengths that hold the third regime across the morph,
  and the bell's reflection fit at 44.1, 48, 88.2 and 96 kHz
  (`tools/trombolese_reference.py`). To play notes, each MIDI note from 34 to
  84 has an embouchure and slide position found by playing it, as a
  trombonist finds a partial and a position (`tools/TromboleseCalibrate.cpp`).
  The worst is 32 cents off; notes the bore can't sound play an octave up.
  The instrument is monophonic: a note played over another slides to it.
  Bore (cylinder to cone, the mod wheel adds to it), Reed (lips to double
  reed), Breath, Attack, Release, Slide Time, Volume; pitch bend moves the
  slide two semitones. About a tenth of a core at 48 kHz.
- **Updating them**: `Engines/engines.json` and `tools/update_engines.py`
  (docs/UPDATING-ENGINES.md).

## Consequences

Tested: Midular takes the chosen clip through its own importer and plays it,
and plays it again after its state is saved and loaded; MIDI Production
Center takes it from its menu and from its own Load Item button, and a pad
plays; Good Idea's engine makes ideas, auditions them on their channels and
inserts them as one undo step; ScaleView reads the chord and leaves the sound
unchanged; Trombolese matches the Python, sounds its notes within 30 cents at
three bore positions, slides, stays finite and falls silent.

Not yet tried on the Mac. Trombolese's level varies from note to note in the
model; each note's measured level evens that out. Midular's importer, asked
with nothing chosen, would ask for a `.mid` file; in Kalibre it says to
choose a clip instead.
