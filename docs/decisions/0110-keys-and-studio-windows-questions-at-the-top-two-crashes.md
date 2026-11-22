# 0110. The keys and the studio in windows of their own; questions at the top; two plug-in crashes

## Context

Asked (22 November), with two crash reports from the Mac:

- The Sounds tab looked through its folders every time Kalibre came back to
  the front (0109): only when Kalibre opens, please.
- The keys were in two places, the browser's Keys tab and Help > Keys: under
  Help only. The browser's Studio tab and its options: in the File menu.
- The Save question was still covered by plug-ins: put questions near the
  top of the screen.
- Crash 1 (signal 11): inside **Groove Agent SE** as it was deleted, from
  JUCE's graph letting go of its old render sequence on a timer
  (`RenderSequenceExchange::timerCallback` → `VST3PluginInstance::cleanup`).
- Crash 2 (signal 5, a trap): inside **HeartCore**, AppKit refusing the
  size it set on its view (`_NSViewValidateGeometry`), as a new device panel
  opened its plug-in's window in its own constructor (`DevicePanel::update`
  from `DevicePanel::DevicePanel`), before the panel was in any window.

## Decision

- **Sounds**: looked through once as Kalibre opens; then only with Look
  Again or a change of folders. Coming back to the front does nothing (0109's
  "at most once a minute" taken out, at the musician's request).
- **Keys**: Help > Keys… opens the **Keys window**; **Studio**: File > Studio ▸
  Sync and Control Surfaces… and AI Agents… open the **Studio window** (the
  latter scrolled to AI Agents). Both are settings windows of their own, as
  the Plug-in Manager and Library Folders are (0002 amended); ⌘W and Esc
  close them. The Kalibre menu's Sync and Control Surfaces… and AI Agents…
  are gone (one place each). The browser keeps four tabs in two rows:
  Plug-ins, Presets, Sounds, Audio. A workspace saved showing the old Keys
  or Studio tab (numbers 5 and 3) opens on Plug-ins.
- **Questions** (`ui::present`) are placed near the top of Kalibre's window,
  just under the top bar, across the middle, and plug-ins in windows of
  their own (0056) now step aside too, not only those in the rack. The
  Mac's **Open and Save panels** are put near the top of the screen, across
  the middle, the first time each comes forward, at the modal panels' level
  so no plug-in's window is in front of them (`FilePanelsMac.mm`). Moved by
  hand, a panel stays where it was put.
- **Crash 1**: a removed node was kept for a second and a half, then let go
  of whatever else held it, so the plug-in was deleted whenever the last
  holder let go: often JUCE's graph, from its own timer, at a moment Kalibre
  didn't choose, and possibly with a window of it still open. Now the engine
  deletes it itself (`Engine::letGoOfOld`), one at a time, only once its
  reference is the last (JUCE's graph and the plan have let go) and no
  window of the plug-in is open. A test failed first (JUCE's own assertion
  that a plug-in was deleted with its window open fired in it).
- **Crash 2**: a panel's plug-in window opens only once the panel is in a
  window on the screen (on the Mac; elsewhere, once it's in the rack); the
  panel asks again when it's put in one (`parentHierarchyChanged`). A plug-in
  that measured the window it was put in found none. A test (with the Mac's
  rule switched on) failed first.

## Consequences

- Crash 1's cause inside Groove Agent SE isn't certain from the report: the
  stack is the plug-in's own clean-up. What Kalibre controls (when, where,
  window closed first, nothing else holding it) is now fixed; if it crashes
  again, its report will show `Engine::letGoOfOld` as the caller.
- A plug-in removed while no audio plays is kept until JUCE's graph plays a
  block (it lets go then): memory for a while, never a crash.
- Not tried on the Mac here: where the Save panel appears, and HeartCore and
  Groove Agent SE themselves. Tests: the four new ones above, a question
  placed under the top bar with a plug-in's own window hidden, and the menus.
