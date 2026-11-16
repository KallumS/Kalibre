# 0058. Plug-ins fitted to the rack; swipes; effects filed right

## Context

Testing on the Mac (16 November, evening), with CUBE, Model 77 and Falcon:
plug-ins taller than the lower zone were cut off at the bottom, hanging out
of their panels over the rack's scroll bar and the window's edge, with no
way to see the rest. Asked first for a scroll bar for them, then instead:
"scaled proportionally to fit within the rack; stretching should be
avoided". Also asked: two-finger swipes to move along the rack, a
right-click menu with Add Track in the track panel, and where Match
Loudness had gone (BitFuzzer showed no Match button).

Why they were cut off: on the Mac a plug-in's window is a native view, and
JUCE places it at its whole area in the window, whatever the scrolling
areas around it hide. 0056 let a tall one "scroll" inside its panel, but
nothing clipped the native view, so it simply hung out of the panel.

Why Match was missing: JUCE describes a VST3 from its moduleinfo.json, which
has no channel counts, so every such effect had 0 inputs and 0 outputs, and
Kalibre took "no audio in or out" to mean a MIDI effect. BitFuzzer was filed
as a MIDI effect: put before the instrument, so it heard no sound at all, and
Match is only offered to audio effects.

## Decision

- **Fitted, never stretched** (0056's "no scaling" is replaced). The room
  for a plug-in is the zone's height under its header, and the rack's
  width. A resizable plug-in is asked to take that size, its shape kept: it
  draws itself at that size, so it stays sharp. Whatever is still too big
  (it can't be resized, it keeps to sizes of its own, or it chose its own
  size since) is shown smaller, the same both ways. Never shown bigger than
  it is: a picture made bigger blurs. The panel's subtitle says "72% to fit"
  when it's shown smaller. Dragging the lower zone taller shows it bigger.
- **How it's shown smaller, and clipped** (`App/PluginClip.*`): on the Mac
  the plug-in's window goes inside a native view of Kalibre's that clips
  what's in it to its bounds (a view doesn't since macOS 14 unless it says
  so) and whose bounds are its frame divided by the scale, so AppKit draws
  the plug-in's own view scaled, and hands it the mouse in its own units. It
  works for plug-ins that ignore being asked for another size, which is why
  scaling failed before (0056). That view is only as big as the part of the
  panel the rack shows, so a plug-in half scrolled out of the rack is cut at
  the rack's edge too. Off the Mac it's a JUCE transform.
- **Two-finger swipes** sideways anywhere over the rack move it, over the
  plug-ins' windows too (on the Mac a local event monitor sees them before
  the plug-in does). Each swipe is judged sideways or not as it starts, so
  scrolling up and down a plug-in still reaches the plug-in. When everything
  fits, the swipe goes to the plug-in as before.
- **Right-click under the tracks** (track panel or lanes): Add Track, Add
  Folder, Add Return, Add Summing Track, Add Cue Track. The new track goes at
  the end, outside any folder. A track's own menu still has them too.
- **Effects filed right**: no audio in or out means a MIDI effect only for
  formats that report it; a VST3 is a MIDI effect only if its category says
  so. Projects saved with a VST3 effect filed as a MIDI effect are put right
  when they're opened (the effect moves after the instrument). Match Loudness
  stays on audio effects only: an instrument has no input to match.

## Consequences

Tested: the role of a VST3 effect without channel counts, and a saved
project with BitFuzzer before its instrument opened with it after. On Linux:
built-in editors fitted into a short zone (shown smaller, shape kept) and
the rack's layout. Not yet confirmed on the Mac: the native scaling and
clipping, and the swipes; they need the Mac and real plug-ins. A plug-in
that measures the mouse in screen coordinates may misplace its own pop-up
menus while shown smaller.
