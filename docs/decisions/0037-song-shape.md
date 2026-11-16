# 0037. Sections, ripple, linked clips, groups and the clip launcher

## Context

Arranging a song by its parts, and playing ideas in loops before arranging them.

## Decision

`Core/Song` (tested):

- **Time edits**: insert bars, delete a stretch (closing the gap), duplicate or move a stretch. Clips crossing an edge are split; automation, markers, sections, tempo changes and meters move with it.
- **Sections** are named stretches in the Sections lane under the ruler; right-click to add, rename, loop, duplicate, move earlier or later, or delete (with or without their bars). Shift-drag one to move it to any bar (asked for 16 November): an outline shows where it lands, on a bar line; everything in it moves with it, and what was there moves out of its way (cut at the section's edges if it was inside). One undo step; the section keeps its identity.
- **Ripple editing** (Edit menu): deleting clips pulls later clips on the same track back.
- **Linked clips** (clip menu > Duplicate Linked) share a link id; after every change the controller passes a linked clip's changed notes or audio to its twins. Their positions and lengths stay their own.
- **Track groups** (track menu > Group): volume and pan move by the same amount, mute, solo and arm together; also for faders dragged and surfaces.
- **The clip launcher** (lower zone > Launcher, G): slots a track a scene. Launching a slot replaces the track's arrangement from the next bar with the slot's clip looped; it's compiled into the track's playback list (Playback), so the engine needs nothing new. Capture Scene copies the loop range into a scene. What's playing isn't an undo step and isn't saved.

## Consequences

- Slots can be edited in the piano roll like any clip.
- Launching is quantised to the bar; there's no other quantisation choice yet.
