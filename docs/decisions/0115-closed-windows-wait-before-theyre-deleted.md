# 0115. Closed windows wait before they're deleted

## Context

A crash report (22 November): Kalibre stopped inside macOS's own handling of
a scroll, `-[NSWindow(NSEventRouting) _latchViewForScrollEvent:]`, with
nothing of Kalibre's on the stack. macOS goes on sending a two-finger
scroll's glide to the window the scroll began over; a window freed meanwhile
is messaged all the same. Kalibre deletes some windows as they close: a
plug-in moved into a window of its own (0056), DAW Battle Pass's (0111), an
X/Y Morph corner's (0114). Scrolling in one, then closing it, fits.

## Decision

A window that closes is emptied and hidden at once (whatever it showed, a
plug-in's window or the battle pass, is closed by its owner as before, the
plug-in told first) and the window itself is deleted ten seconds later
(`editors::retire`). Settings windows were only ever hidden.

## Consequences

Tested: a plug-in's own window docked is emptied and hidden at once, its
plug-in's window gone, and deleted later. The cause isn't certain from the
report; if it happens again, what was being done just before tells.
