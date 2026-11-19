# 0095. The timer's away rules, what the scan hears, clips backwards and upside down, the Sampler's slices and grains, the transport at the bottom

## Context

Asked (19 November, evening), with "no one is using the DAW yet, so it's
safe to make any changes":

1. The timer (0091) came from DAW Battle Pass, whose timer stops when no
   input is detected, so it only counts real music-making: copy that over.
2. The Sounds scan should hear each sample's volume, pitch and tempo, and
   keep them in a file for later sessions; a button to stop a sound playing
   when it's chosen.
3. The clip menu: reverse, invert (flip it upside down), and split an audio
   clip at its transients into separate clips. Transient detection,
   granular synthesis and speed in Kalibre Sampler. Import samples onto the
   timeline split by transients, from a right-click menu.
4. Shift-drag resizes a clip.
5. A double-click on the three dots opens the browser or lower zone at its
   first size.
6. Plug-ins dragged from the browser into the device rack.
7. Sample rates up to 192 kHz.
8. The transport buttons (Go to Start, Play, Record, Loop, Punch, Click,
   Count-in) at the bottom centre, below the lower zone.

## Decision

1. **The timer.** 0091 had already ported the battle pass's idle-aware
   clock (3 minutes, 8 while playing, recording always counts). What
   differed: the battle pass (`W.signals` in reascripts/bp_watch.lua) only
   counts the mouse while REAPER is the app in front, and its Stats page
   sets "Minutes before you count as away" (1 to 15). Kalibre's global
   mouse listener heard the pointer move over other apps' windows (JUCE
   reports every move of the pointer to a global listener, over a Kalibre
   window lying under Safari too), which kept the clock going while the
   musician was elsewhere. Now the mouse counts only while Kalibre is the
   foreground app (`Process::isForegroundProcess`), and right-click the bar
   > **Pause the Timer After** sets 1, 2, 3, 5, 10 or 15 minutes (kept with
   the window's settings, not the song). `WorkClock::setAwayMinutes`.
2. **What the scan hears** (`LibraryScanner::listen`, up to a minute of each
   sample): its integrated loudness by `loudness::Meter` (BS.1770, the
   master's meter; a mono file counted once), its loudest sample, its note
   (`audiotools::noteOf`: YIN on up to 16 windows of its sounding part,
   their median, only when most agree within a semitone: drums and chords
   have none), and, when neither the name nor an ACID chunk gives a tempo,
   one heard (`audiotools::tempoOf`: the onsets' autocorrelation, 60 to 200
   BPM leaning to 120; twice as fast when the half beat repeats as strongly).
   A heard tempo is trusted only when the file is a whole number of bars at
   it (within 3%), which then makes it exact; otherwise it's shown as
   "about", and Sync doesn't stretch it (a guess could be double or half).
   All of it goes into `Library Index.json`; `Item::analysis` says which
   scan read it, so files read by the older scan are listened to once, and
   never again until they change. **Auto-Play** (saved with the library's
   settings): off, choosing a sound is silent, and a click on the chosen
   sound's picture plays or stops it.
3. **Clips.** `edit::reverseClip`: MIDI notes and controllers mirrored
   within what the clip plays; audio's `reversed` (as before). 
   `edit::invertClip`: MIDI notes mirrored between the highest and lowest
   ("upside down"); audio gets `AudioData::inverted`, played as a negative
   gain on its region (saved; Playback's region gain). Both on every chosen
   clip, one undo step. `edit::splitAtSource` cuts at each hit, from the
   last back so the clip keeps its id; `Controller::splitAtTransients` reads
   the hits as Quantise Audio does. Not on a looped or reversed clip.
   **Import Split at Transients**: `importFile` takes a flag, and the cuts
   are made inside the same `apply`, so it's one undo step; from the Sounds
   tab's right-click (**Put on the Chosen Track, Split at Transients**) and a
   lane's (**Import Audio Here, Split at Transients…**).
   **Kalibre Sampler** gains **Mode** (Keys, Slices, Granular), **Speed**,
   **Slice Sensitivity** and a Grains group (Size, Density, Spray, Position).
   Slices: the transient detector was split into `hitCandidates` (every
   moment that could be a hit, with its rise and level, made once on load)
   and `isHit` (a sensitivity's rule), so the audio thread finds a slice's
   bounds without allocating, whatever the knob says. Granular: per voice up
   to 24 Hann-windowed grains, a reading head moving at Speed, each grain at
   the key's pitch; no allocation. Speed in Keys and Slices is tape speed.
4. **Shift-drag** anywhere on a clip resizes it from the nearer edge, the
   edge following the pointer from where it was (not jumping to it). A
   Shift-click without a drag still chooses (0053), and makes no undo step.
5. **Double-click on the dots** of either handle (shut or open) opens that
   panel at 320 points, its first size.
6. **The device rack is a drop target** for the browser's `plugin:` drags:
   added to the track shown (a new one if none) between the panels either
   side of the drop; `edit::normaliseChain` still keeps MIDI effects,
   instruments, audio effects in order. Outlined while dragged over.
7. **192 kHz**: nothing in Kalibre capped it. The Audio tab lists what the
   interface reports (on the Mac, Core Audio's nominal rates: 192 kHz when
   the hardware has it), and Kalibre plays, records and exports at the
   device's rate. A test runs the engine on an interface at 192 kHz and
   renders a sampler in tune and in time. Not added: a song rate apart
   from the interface's (rendering at another rate would need every warped
   clip made again at it).
8. **The transport moves to a `TransportBar`** along the very bottom, its
   buttons in the middle, under the lower zone (and under its shut bar).
   The rule "nothing important at the bottom of the window" (0070) is
   amended for it: the musician asked for it there. The message line stays
   at the top. Loop, Punch, Click and Count-in can still be hidden from the
   bars' right-click menu; the top bar keeps the position, tempo, time,
   grid, scale, chord, timer, Follow, CPU and delay.

## Consequences

- Tested: the clock's away minutes; loops at nine tempos and one, two, four
  and eight bars heard exactly, noise and one-shots not; notes in tune and a
  quarter-tone off, noise and silence none; the index's new fields round
  trip, an old scan's item read again, a second scan reading nothing; Auto-
  Play off silent, its picture plays; Reverse and Invert for MIDI and audio
  (a negative region gain); Split at Transients and the import split, one
  undo step; the Sampler's slices by pitch and the end of a slice silent, a
  sensitivity changing them, Speed as tape, Granular in tune at half speed
  and held at Speed 0; Shift-drag both edges, a Shift-click changing
  nothing; double-clicks on both handles; drops onto the rack in place;
  192 kHz; the transport's place, centring and hiding.
- The heard tempo leans on a loop being cut to whole bars of four beats; a
  3/4 loop or one with a pickup is "about". The note of a drum loop with a
  steady kick is the kick's.
- Not yet tried on the Mac: the foreground check with plug-in windows in
  front (they're Kalibre's own, so they count), grains' CPU with many
  voices, the transport with a trackpad.
