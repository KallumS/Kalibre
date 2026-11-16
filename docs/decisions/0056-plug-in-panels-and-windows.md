# 0056. Plug-in panels: own size, their own windows on request, Match Loudness

## Context

Testing on the Mac (17 November): some plug-ins were cut off or not centred
in the rack, and adding another device turned some blank. The user also
asked for Match Loudness on every plug-in's panel, and a button to move a
plug-in from the rack into a window of its own.

## Decision

- **Closing a plug-in's window properly.** JUCE 9 leaves it to the host to
  tell the plug-in its window is going (`editorBeingDeleted`) before deleting
  it. Kalibre didn't: the plug-in went on thinking its old window was open,
  so the next time Kalibre asked (every panel was made again whenever a
  device was added) it got nothing, and a generic panel stood in. Every
  plug-in window is now opened and closed through `Engine/Editors.h`.
- **Panels are kept** while their plug-in stays: adding a device makes only
  its own panel.
- **No scaling.** A plug-in's window is shown at its own size, centred; a
  resizable one is fitted to the zone's height keeping its shape. One taller
  than the zone scrolls. (Scaling was ignored by many plug-ins, so the panel
  was sized for a scaled window that wasn't: cut off, or off-centre.) When a
  plug-in resizes its own window, its panel follows.
- **Its own window** (the ⧉ button): the plug-in's window moves out of the
  rack into a window of its own, titled with the plug-in and the track; the
  panel says where it went. Closing that window, or the button again, brings
  it back. Removing, switching off or replacing the plug-in closes it. This
  is a window for a plug-in, asked for by the user: 0002 is amended again.
- **Match Loudness** on every audio effect's panel (not the Container's,
  which has its own for its whole chain): what comes out is kept as loud as
  what went in, by short-term loudness (LUFS-S), as in the Container (0055),
  in the plug-in's wrapper (TimedPlugin), so it's in exports too. Saved with
  the device; the button's tip says how much it's doing.
- Tooltips: Kalibre had a tip for nearly every control but nothing to show
  them. They're shown now, in every window. The bar under a chosen button's
  label is gone, at the user's request.

- **Questions aren't covered.** On the Mac a plug-in's window is a native
  view, drawn over anything Kalibre draws inside its window, so it covered
  "Save the changes?" when quitting, with no way to bring it forward
  (reported 17 November). While a question is asked (`ui::present`), the
  plug-ins' windows in the main window step aside, and come back when it's
  answered.

## Consequences

Tested: open, close and open a plug-in's window gives its own each time;
adding a third device leaves the first two's windows exactly as they were,
none generic; a plug-in turning the sound down 10 dB with Match Loudness on
exports at the input's level. In the running app: out to its own window
(its own editor) and back.
