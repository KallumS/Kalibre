# 0080. Freeze, quietly; Bounce in Place

## Context

Asked (18 November): a bounce in place for clips on the timeline; and
freeze, which quietly renders a track to audio and unloads its instruments
and effects, unfreezing deleting the render and restoring the plug-ins'
states. Freeze existed (0038 era), but it rendered a copy of the song with
the other tracks taken out, so every other track's plug-ins were unloaded
and loaded again around it, and unfreezing left the render on the disk.

## Decision

- **`Engine::renderTrack`**: the whole song is synced, every plug-in kept,
  but only the one track is wired out, straight from its fader to the
  output; the rest are left unconnected. An offline plan (0078) plays only
  what can reach the output, so the others cost nothing, and the render's
  delay is the plan's count of that one path.
  `Controller::renderTrackAudio` sets the track at unity (no volume, pan or
  mute automation, no sends, nothing soloed), optionally keeps only some of
  its clips, and writes 32-bit float WAV (0079).
- **Freeze** (track menu): the track rendered from the start of the song to
  its last clip's end and four seconds more; it then plays the render and
  its plug-ins are unloaded, their states kept in the song (0010). Nothing
  else reloads. **Unfreeze** deletes the render and loads the plug-ins as
  they were. Undo or Redo that brings a frozen track back with its render
  gone renders it again.
- **Bounce in Place** (right-click clips, several with Shift- or
  Option-click): each track's chosen clips rendered together through its
  plug-ins, from the first one's start, the tail kept to where it falls
  silent (up to four seconds past the end); onto a new audio track just
  under it, made like it (volume, pan, output, sends, colour, in the same
  folder), named "<track> Bounce". The clips are muted, not removed. One
  undo step. A frozen track's clips aren't bounced (its plug-ins are
  unloaded): it says to unfreeze first.

## Consequences

Tested: freezing one track leaves another track's synth instance the same
one; the render is 32-bit float and not silent; unfreezing deletes it and
the synth's setting is as it was; Undo renders it again. A bounce: a new
track under the clip's, at its volume, holding a float WAV with sound in
it, the clip muted, the other clip not; one undo step.

Freezing still pauses playback while it renders (the plug-ins are the
playing ones); the bigger the song, the less it costs now, as only the
frozen track plays. Sidechains from other tracks are heard in a freeze or
bounce: from a track's fader as before, and (since 18 November, later) from
folders and returns, as rendering one track now wires the rest as ever with
the master going nowhere, and the offline plan plays only what it hears.
