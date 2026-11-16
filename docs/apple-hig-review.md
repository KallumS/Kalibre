# Review against Apple's Human Interface Guidelines: Foundations

Kalibre checked against the Foundations pages, as the family's windows were
(Good Idea's review is the model). Kalibre is a native JUCE app rather than a
ReaImGui script, so more can be met; where something isn't, it says why.
`Tests/App/TestWindow.cpp` holds the measurable ones.

## Met

| Guideline | How |
| --- | --- |
| Text at least 4.5:1, aiming at 7:1 (ACC-10, DRK-07) | Every word is 7:1 or more on what it sits on: body `#DDE1E7`, secondary `#B0B6C0`, warnings `#FF9E96`, ink on every button. Tested |
| Icons and things to grab 3:1 (ACC-11) | Edges `#8A919C` 4.7:1, scrollbar thumb 3.6:1, tick boxes edged. Tested |
| Never colour alone (ACC-15, COL-08) | Chosen buttons are yellow only (the bar under the label was taken off at the user's request, 17 November, a known departure); chosen notes a bar along their foot; drum hits a velocity bar; the played track an accent bar; recording a ring around Record; warnings are worded |
| Larger text (ACC-06, TYP-16) | View > Text Size 100–200% (Cmd-= / Cmd-- / Cmd-0), remembered; the transport wraps to two rows rather than cropping; the editors scroll. Tested at the smallest window and 200% |
| Control sizes (ACC-22, ACC-23) | 28 pt in the transport and dialogs, never under 20 pt (track headers, tabs), as Apple allows for dense Mac windows |
| macOS type: 13 pt body, never under 10 (TYP-01) | 13 pt body, 11 pt for dense labels (Apple's Subheadline) |
| System font (TYP-07, TYP-08) | The system sans; no fonts shipped |
| Keyboard (ACC-30) | Every command has a menu item; the common ones have keys, shown beside them; Space presses a focused button; Escape lets go; the X/Y pad moves with the arrows |
| Don't override system keys (ACC-31) | Only the usual app keys |
| No time-outs (ACC-33) | The message line stays until dismissed |
| Undo hard-to-undo actions (ACC-43, DP-06) | 500 undo steps; deleting versions, tracks and notes is undoable; messages offer Undo |
| Nothing important at the bottom edge (LAY-16) | Messages under the transport |
| Progressive disclosure (LAY-04) | One lower-zone view at a time; folders fold; the browser hides |
| Raised surfaces lighter in Dark Mode (DRK-14) | Menus and tooltips `#2A2F37` edged `#3A404A` |
| Dark only, for a media app (DRK-04) | As the family; studio use |
| Tooltips break at about 64 characters (LAY-14) | `theme::wrapTip`, tested |
| VoiceOver names for icon controls (ICN-10) | ▶ is "Play", ● "Record", M "Mute", ⏻ "Plug-in On"; JUCE's accessibility exposes them |
| Menus in the menu bar (BRA-07) | 0027 |
| Writing: verbs on buttons, "click", no "we", no "Oops" (WRI) | Followed throughout |
| Privacy (PRV) | Nothing is collected or sent; settings in Application Support |

## Not met yet

- **Increase Contrast and Reduce Motion** are not read yet (Miderator's `SystemSettings` shows how; next).
- **Native views of plug-ins** are the plug-ins' own; their contrast is theirs.
- **Testing on real screens** (COL-04, COL-05, TYP-02) needs the Mac.
