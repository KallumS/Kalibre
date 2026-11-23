# 0134. The UAT's smaller findings, and Kalibre's own devices in every slot

## Context

The full UAT of 23 November (0133) left a list of smaller findings and three
questions. The musician's answers: Delete keeps deleting the chosen tracks
(no change); **yes**, every slot that takes a plug-in should offer Kalibre's
own devices too; the scroll wheel over the mixer stays as it is. Then: fix
all of the smaller findings.

## Decision

**Kalibre's own devices in every slot** (the musician's answer). A drum
pad's Plug-in ▾, Offline Effects, the Container's, Multiband's and Stereo
Split's slots and X/Y Morph's corners list, after any scanned plug-ins,
"Kalibre: Instruments" or "Kalibre: Audio Effects" with Kalibre's own of
that kind (`pluginmenu::forSlot`, from one list, `pluginmenu::ownDevices`,
which + Add uses too). Left out: the devices that hold plug-ins themselves
(a Container in a Container's slot) and Hardware Insert.

Fixed, each with a test that failed first (numbers as in the UAT's README):

- **Devices** (6–13, 23–26, 44, 45). Kalibre Drums: a pad's name opens its
  menu on a right-click; a beat dragged to the timeline keeps its swing and
  gate. Euclidean: each dial named (Steps, Hits, Rotate), every row's dials
  in line. Drum Pads: all sixteen pads in a short zone. A device panel is
  wide enough for its name, switched off too. ScaleView starts on the song's
  scale. Modulators: each knob named; the shape drawn again when chosen.
  Kalibre's knob panels go to one row of knobs in a short zone; an Off/On
  knob switches with a short drag; a panel shown under 60% says "⧉ shows it
  bigger". The ReaImGui tools fill their panel's height.
- **The editor bar** (14): too narrow for one row (1440 points with the
  browser open), the editor's tools go on a second row.
- **The lane under the notes** (16): its menu has Clear Lane (a controller,
  pitch bend, pressure, or one part of each note's MPE expression).
- **Notation** (17): bars fill the staff (up to four across); a note's start
  and end are read on the nearest 1/32 or 1/16 triplet (`notation::shown`),
  so humanised notes read as the notes they were. The notes aren't moved.
- **Steps** (19): a double-click to tie undoes what its first click did; the
  tie is one undo step.
- **MIDI files** (20): whole bars counted from where the file is put, not to
  the next bar line.
- **The articulation lane** (29): each articulation's colour deepened under
  its name to 7:1 (`theme::underWords`).
- **Split at Transients** (30): a sound already there at the very start is a
  hit (it never was, so the Sample Editor counted one fewer than the clips
  made); the message says clips and hits, and the lead-in before the first
  hit when there is one.
- **A frozen track** (32) says Frozen under its name.
- **Recording** (37): the take grows on each armed track, in the arm's
  colour, from where recording starts to the playhead
  (`Controller::recordingTake`); none during the count-in.
- **The chord lane** (38, 43): the lit chord is drawn again whole when it
  changes, not only in the playhead's strips; only a solo of a track with
  notes, or of a folder holding some, says which notes count (soloing a
  return emptied it).
- **The ruler** (53): a marker's name goes after the bar number, the number
  after the marker's flag (`Arrange::markerNameArea`).
- **Themes** (5): choosing one offers no Undo beside its message.
- **The mixer** (35): the Master and Cue strips stay in sight beside the
  loudness, however many tracks scroll. The Master's level was always there
  (a test plays and reads it): its strip had scrolled past the end.
- **Export** (51): the save panel goes first, then the message line counts
  up as the file is written (`Controller::onExportProgress`, drawn at once).
- **Script tools** (46): a ReaImGui or gfx tool runs no frames while its
  window isn't on screen, and after a frame that took over 8 ms waits three
  times as long before the next.

Not a fault: 1 (a Studio tooltip cut off): the tip is kept on the screen;
on the virtual screen, with no window manager, the main window drew over
it. "Share Start and Stop" had no tip; it has.

## Consequences

- A slot that takes a plug-in builds its menu with `pluginmenu::forSlot`.
  A new device of Kalibre's goes in `pluginmenu::ownDevices` once.
- Anything that reads notes for notation reads their places through
  `notation::shown`.
- A script tool's editor does nothing off screen; anything that needs a
  tool's frame without a window calls `ToolSession::frame` itself.
