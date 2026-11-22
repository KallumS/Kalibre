# 0116. Drops onto a device's window in the rack

## Context

Reported (22 November): dragging a sample from the browser onto Kalibre
Sampler did nothing. On the Mac a device's window in the rack is shown in a
native view of its own, scaled to fit (0058); Kalibre's drags look for a
target in Kalibre's window and never reach a component inside that view.

## Decision

Each device's panel in the rack is a drop target itself: whatever its
device's window would take (a sound for Kalibre Sampler, a sound on a Kalibre
Drums pad), the panel hands on, with the pointer's place turned into the
window's own units (`PluginClip::toContent`), so a pad gets the drop it's
under. Plug-ins dragged in from the browser still go to the rack.

## Consequences

Tested: a sound dropped on Kalibre Sampler's panel in the rack loads it. Not
tried on the Mac itself; what the test can't show is the Mac's native view.
