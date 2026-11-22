# 0113. DAW Battle Pass in Kalibre's look

## Context

Asked (22 November): make the battle pass's window match Kalibre's style,
"the rounded boxes instead of rectangular". Its colours were already the
house scheme Kalibre shares; its buttons, tabs, boxes to tick and type in,
and its gradient banners were square, and a chosen button carried a bar under
its label, which Kalibre's lost at the musician's request (0056).

## Decision

In Kalibre's version of the window script (0112):

- Every ImGui control (buttons, tabs, checkboxes, sliders, text boxes,
  popups) rounded 6 points, Kalibre's `cornerRadius`.
- No bar under a chosen button: the accent alone says it's chosen, as in
  Kalibre.
- The gradient banners and card tops (the season's banner, a deal's top, a
  gear tile) rounded too. ImGui can't round a gradient, so Kalibre's ReaImGui
  takes a tenth argument on `DrawList_AddRectFilledMultiColor`: a rounded
  box, each point coloured by where it lies between the four corners.
- Everything else it draws was already rounded (6 to 8 points).

## Consequences

The rules and words are as in 0112. Rendered again (45, 46); the battle
pass's tests pass.
