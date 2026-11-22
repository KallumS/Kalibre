# 0112. DAW Battle Pass in Kalibre's words

## Context

Asked (22 November), after 0111: adapt DAW Battle Pass to Kalibre. Its
script said REAPER, project, media item, region, edit cursor, "plugins", and
offered to start with REAPER. Kalibre carries the family's scripts unchanged
and sends a fix to the script's repository first; the Ableton version is a
branch of the battle pass's repository. Writing to that repository wasn't
allowed from here (the session's access was refused).

## Decision

- **Kalibre's own versions** of the four files whose words the musician
  reads: the window (`DAW Battle Pass.lua`), `bp_game.lua`, `bp_loot.lua`
  and `bp_watch.lua`, in `Engines/tools/daw-battle-pass/`, listed as
  `standins` in engines.json, as Kalibre's place stand-ins are (0068, 0093):
  `tools/update_engines.py` says when the original changes, to make the same
  change here. Each says at its top what's different. `bp_theory.lua` and
  `bp_fx.lua` stay the family's, unchanged.
- **Words**: REAPER is Kalibre; a project is a song; a media item a clip; a
  region a section (Add Sections, which lays an arrangement out in the
  Sections lane); the edit cursor the cursor; the selected track the chosen
  one; plugins plug-ins (Kalibre's spelling); "stock REAPER plugins" Kalibre's
  own devices; this computer this Mac. The welcome says it runs whenever
  Kalibre is open, window shut or not (it did say keep the window open and
  dock it). What it watches says Kalibre in front, keys included.
- **Taken out**: Start the Battle Pass When REAPER Starts. It always runs with
  Kalibre.
- **Unchanged**: every rule, number, reward, quest and achievement, and the
  save's format, so REAPER's progress still opens here (its quests already
  chosen keep the words they were made with until the next ones).

## Consequences

- The battle pass's own suites, run on Kalibre's versions, pass but for the
  three checks of words and features changed here ("(unsaved project)",
  Add Regions, ticking Start with REAPER). A test in Kalibre failed first on
  the original files: what the musician reads says Kalibre, song and
  section, never REAPER, project, media item, region or plugin, and the
  Stats page never touches REAPER's start-up script.
- If the battle pass's repository is opened to this work later, these files
  could become a `kalibre` branch there, as the Ableton version is, and
  Kalibre carry that branch unchanged.
