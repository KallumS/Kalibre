# 0109. What the UAT song found, and how each was fixed

## Context

Asked (22 November): use Kalibre as it is, on the Linux machine, to make a
simple song with a variety of devices and ways of putting notes in, as a
user acceptance test. The song: four bars in A minor at 100 BPM, five tracks.
Kalibre Drums' steps (Random Samples, Drag to Timeline), Kalibre Synth drawn
in the piano roll, Kalibre Sampler playing a MIDI file dragged from the Sounds
tab (Random Sample, then a pad dropped on it), Trombolese written by an AI
agent over MCP, an arpeggio made by a Lua script; two Reverberators; saved
and exported. Then asked: fix them all.

## Decision

Each found, and fixed with a test that failed first:

- **Trombolese blew up** (the worst): sliding from a held D5 to B4 went 30 dB
  over full scale; held alone, its notes ranged from 0.02 to 4.2. Its note
  table's levels differ up to four hundredfold between neighbours, and a
  slide sounds the old note's regime at the new note's level. Its level now
  slides in decibels, and its peaks are followed (caught on the sample, let
  go over 150 ms) and held under a ceiling (0093 amended). The voice itself,
  checked against the Python, is unchanged.
- **Octaves named two ways**: the piano roll, the Sampler and the tuner said
  middle C is C4; the Sounds tab, Random Sample and the drum pads' note menu
  said C3. C4 everywhere now.
- **Knob panels' headings walked down** every time they were drawn (the box
  was cut, not a copy of it).
- **Kalibre's own devices shrank out of reading** in a short lower zone: they
  were fitted keeping their shape (0058). Now they keep their width (no wider
  than leaves + Add in view) and take the zone's height, down to their
  smallest; the Sampler's waveform gives way first. Others' plug-ins keep
  0058's fit (amended).
- **Kalibre Drums' eighth pad was cut off**: the pads scroll.
- **The piano roll showed nine keys**: the velocity lane is a fifth of the
  height (32 to 64 points), the CC lanes at most a third of the rest, and the
  notes always keep 160 points.
- **Export the whole song** ran to the view's 16 bars (38 seconds for 10):
  it ends at the bar line after the last clip now (`Project::songEnd`), the
  tail after that (0097 amended). The menu, Export Several at Once and the
  agents' export all.
- **Double-click rename closed at once**: the arrangement refreshed, setting
  every name, which closes a name being typed. Not while it's typed now.
- **A dragged beat landed a sixteenth late**: let go within a beat of a bar
  line, it starts on the bar.
- **⇧⌘E did nothing** until something was clicked: keys reaching the window
  with nothing focused (after a dialog) go to Kalibre's shortcuts.
- **"502.246 Hz", "27.9536%"**: juce::String takes 0 decimals to mean all of
  them; `ui::number` rounds. The agents' parameter text gave its unit twice.
- **Kalibre Synth had no presets**: eleven factory presets (Init, Warm Bass,
  Sub Bass, Acid Bass, Soft Pad, Bright Lead, Pluck, Brass, Keys, Strings,
  Noise Sweep), in Presets > Factory Presets, each one undo step.
- **A bass clip opened in the drum grid**: a clip opens in the drum grid on a
  track playing Kalibre Drums or when its notes came from the drum grid, in
  the piano roll otherwise, when one of those two was open.
- **A held note had a tempo** ("about 120 BPM" on a pad): a tempo is heard
  only in four or more hits now. Sounds scanned before keep what they had
  until they change (hearing half a million again isn't worth it).
- **New files needed Look Again**: coming back to Kalibre looks through the
  folders again, at most once a minute, reading only what's new or changed.
- **A sound dropped on the Sampler wasn't in tune**: dropped or loaded, its
  Root Note is the note the scan heard, as Random Sample's is.
- **Chosen clips dragged one at a time**: dragging one of several chosen moves
  them all as far (Option copies them all), one undo step.
- **Notes wore the chosen track's colour**: the clip's own track's now.

Not changed: the Library Folders window's close button is the Mac's own (the
virtual screen had no window manager to draw it); "Add the Example Scripts"
works in the Mac app (the CI copies them in). The mix still touches 0 dB in a
few places where the test's own drum samples peak together: the faders'.

## Consequences

Tested: each above (236 core, 198 app tests). The song made again: exported
13.9 seconds (the song and its reverbs), no blow-up; the renders.
