# 0092. The toolbox: Control-right-click for Cubase's tools, adapted from Right-Click Toolbox

## Context

Asked (19 November): adapt the user's Right-Click Toolbox for REAPER
(KallumS/Reaper-Right-Click-Toolbar, read only) for Kalibre, so that holding
Control and right-clicking in an area brings up the toolbar and gives the
tools. That repository gives REAPER Cubase's toolbox by rewriting REAPER's
mouse modifiers: thirteen tools, some with modes, a floating toolbar at the
mouse that closes once a tool is picked, the tool's mode menu on clicking
its lit button again.

Asked which areas: the arrangement **and the piano roll**, knowing that
tools in the piano roll change decision 0009's rules while one is chosen.

## Decision

- **Control-right-click** (a real right-click, or a two-finger click, with
  Control held) over the arrangement or the piano roll opens the toolbox at
  the pointer. Control-click alone stays the Mac's right-click, so every
  right-click menu is where it was.
- **Each area has its own tool**, and each tool remembers its mode
  (`Core/Toolbox.h`, `tools::State`; not saved). The toolbox shows that
  area's twelve tools: the Drumstick is the piano roll's only, Time Warp the
  arrangement's. Its own small window, like a menu, so it shows above
  plug-ins on the Mac. Click a tool to use it; click the lit one again, or
  right-click one with ▾, for its modes. It closes once a tool is picked, or
  with Escape or a click elsewhere. A line at its foot says what the tool
  under the pointer does.
- **A tool takes the left click only.** Right-clicks keep their menus, and
  in the piano roll right-click still removes notes (0009). With **Select**,
  everything is as it was; the tools change nothing until chosen.
- **Each tool's pointer** is its icon (the Hand the system's hand, Range an
  I-beam), something REAPER couldn't do. The message line says which tool
  was chosen and what it does.
- **The number keys 1 to 9** choose Select, Range, Split, Glue, Erase, Zoom,
  Mute, Draw and Play, in Cubase's order, in each area that has the tool;
  Line, Hand, Drumstick and Time Warp are in the Keys tab without a key.
  (Digits for step input still win while it's on.)
- **One gesture, one undo step** (0008). Drags that show their change as
  they go (Erase, Mute, drawing automation, Line) edit the project on screen
  and then go through `Controller::applyLive`, which puts the project back
  and makes the change with `apply`. Choosing a tool isn't an undo step.

The tools, as in Kalibre:

| Tool | Arrangement | Piano roll |
| --- | --- | --- |
| Select | As always. Modes: Click Adds or Takes Away; Everything at the Click (every clip playing there) | 0009's rules. Same modes, for notes |
| Range | Drag across tracks for a stretch of time (washed in white); Delete clears what's in it, clips cut at its edges (`tools::clearRange`); Escape lets it go | Drag to choose an area, as Shift-drag |
| Split | Click to cut at the grid (Command: off it; zero crossings when on, 0054). Option-click: pieces as long as the click is from the start. Mode: Split Every Chosen One. A line shows where it cuts | The same for notes |
| Glue | Joins the clip to the next on its track: MIDI clips into one; two halves of one recording back into the clip they were; two different recordings by Consolidate (a new file, 0081), said so | A note to the next of its pitch |
| Erase | Click or drag over clips and automation points | Click or drag over notes |
| Zoom | Click in, Option-click out. Modes: Zoom to the Dragged Area; Zoom Horizontally (drag right/left); Zoom Vertically (every track taller or shorter, an undo step since heights are saved) | The same; vertically the rows |
| Mute | Click or drag over clips; a chosen one mutes every chosen one | Notes, which gained `Note::muted`: kept, saved, drawn hollow, never played |
| Draw | Drag in an empty lane: a MIDI clip that long (a bar for a click); drag over automation to draw it freehand; on a clip, as Select | A note where clicked, even over another, as long as the last; drag to lengthen |
| Line | Drag a line of automation. Modes: Linear, Curve, S-Curve, Fast Start, Fast End, Steps, made with the points' own bend so they can be bent again | A run of notes a grid step apart, the pitch moving by the shape |
| Play | Play While Held (Cubase's Play tool, which REAPER couldn't do), or Play from the Click (click again to stop) | The same on empty space; drag over notes to hear them |
| Hand | Drag to move the view | The same |
| Drumstick | — | Click or drag to paint hits a grid step long, every step passed; click a hit to take it away |
| Time Warp | Click: a tempo change on the nearest grid line. Drag a beat to where it falls in the audio: the tempo from the change before it is set so it lands there, the tempo after it kept; audio playing at its own speed stays where it's heard, so the grid moves under it (`tools::warpBeat`) | — |

Not brought across: Object Selection's Sizing Applies Time Stretch and
Select Objects Behind; Play's Scrub and Jog (Kalibre has no scrubbing);
the Draw tool's Line mode (the Line tool does it).

## Consequences

- Decision 0009 gains a pointer: its rules hold with the Select tool.
- The Eraser in the arrangement sweeps several clips in one drag, which
  REAPER's couldn't.
- Time Warp keeps unwarped audio in time; looped audio clips and warped ones
  follow the beats, as MIDI does.
- Tested: the tools' edits without a window (`Tests/TestToolbox.cpp`), and
  each tool driven with the mouse in a real arrangement and piano roll, the
  toolbox opened by Control-right-click and a tool picked from it
  (`Tests/App/TestTools.cpp`). Renders 28-30.
