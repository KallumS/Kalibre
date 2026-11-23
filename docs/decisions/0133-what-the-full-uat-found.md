# 0133. What the full UAT found, and what was fixed

## Context

Asked (23 November): a full UAT, using every feature of every device and of
Kalibre itself to make a song, on the Linux machine; it needn't be a good
song. Kalibre ran on a virtual screen from a first start, driven by mouse and
keys (`xdotool`), with its pictures taken as it went: `docs/uat/2026-11-23/`.
For the parts that need sound, a second build with JACK ran on JACK's dummy
device (a timer, no sound card); neither build is the Mac's, and both were
Debug builds, slow at drawing.

About fifty things were found: `docs/uat/2026-11-23/README.md` lists every
one, what was done, and what wasn't.

## Decision

Fixed, each with a test that failed first:

- **A question's field didn't have the keys.** Rename the Section opened,
  "Verse" was typed, and its letters went to the window's shortcuts: S soloed
  a track, R started recording. `ui::present` now gives the first field the
  keyboard focus with its text chosen, so typing replaces it.
- **Recording with Count-in put the take a bar late.** The count-in plays
  from a bar before the cursor with recording on, and the take started there:
  its first bar was the count-in's silence. The engine now passes where the
  count-in ends (`Clock::recordFrom`) and a take is written from that very
  sample. Found with it: a take's start was also its "not started yet" sign
  (below zero), so a take from the song's start, less the master's delay,
  was started again every block, a block late.
- **A track armed for MIDI recorded its own clips back.** What the
  instrument is played (0064) includes the track's own clips playing:
  recording with nothing played in laid a copy of the clip over itself,
  every note doubled. Notes that are the track's own clips' (same pitch,
  within 15 ms of when they were sent, its delays and random timing allowed
  for) are taken out; an arpeggiator's own notes still count.
- **Explode Chords copied the Note Splitter** onto every voice's track,
  which sent each voice to the splitter's tracks again. It stays behind.
- **The last touched control was set by code.** Turning Cutoff, then Set B
  and Set A, made Volume (the last control A's state set) the last touched.
  While a state is put back (A/B, a preset, Undo, switching a plug-in on),
  `Engine::QuietTouches` keeps its controls from counting as touched.
- **An agent's request could wait for ever,** and every later one behind it
  (it waited for a message thread busy drawing). After `answerWithinSeconds`
  (five minutes; an export can take a while) the agent is told Kalibre is
  busy and may still do it.
- **An export that clips said nothing.** The mix peaked at +12 dBTP and the
  24-bit WAV was cut off at full scale. A 16- or 24-bit or MP3 export whose
  loudest moment is past full scale now says so, and by how much.
- Smaller: Home, End and forward Delete named as the Mac's menus name them
  (↖, ↘, ⌦; not "home", "end" and ⌫); "Added 1 note" (not "1 notes"); the
  top bar's "Tempo (changes)" fits its box; the Scale menu's heading is
  "Key"; the message line says what the first look through the sound
  folders found, instead of "Looking through…" for ever; a MIDI clip's menu
  has Transpose, as the guide said.

The guide corrected where it was wrong: a CC lane's menu is on its name;
Delete with nothing else chosen deletes the chosen tracks.

Not done, and why, in the UAT's README: things to ask the musician (whether
Delete should delete tracks; Kalibre's own devices in plug-in slots), things
only the Mac can show (the ReaImGui tools' speed with a release build), and
smaller ones left for another day.

## Consequences

- `Clock::recordFrom`: nothing before it is recorded. Anything new that
  records from the live path starts there.
- A state put back by Kalibre goes through `Engine::QuietTouches`; a new
  place that calls `setStateInformation` does too.
- `TrackSource::recordBegan` says whether a take began: its start can be
  below zero.
