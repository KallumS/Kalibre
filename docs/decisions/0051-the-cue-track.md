# 0051. The Cue track

## Context

While producing, the user wants plug-ins on what they hear (a reference
EQ, a headphone correction, a meter that colours the sound) that must never
end up in the finished track (16 November).

## Decision

- One Cue track a project at most (Track > Add Cue Track), kept apart in
  `Project::cue`: after the master in the mixer, standing apart from it.
- Live, the master goes through the Cue track's audio effects, its volume,
  pan and mono, then out. Exporting (mixes, stems, every format) leaves it
  out: the master goes straight to the file.
- It takes audio effects only, holds no clips (no audio or MIDI can be made
  on it), has no versions, sends, solo or polarity; it can be muted.
- Right-click its strip to remove it, or choose it and Delete.
- Every strip in the mixer has a Mono button now. On ordinary tracks it is
  part of the mix (and exported); on the Cue track it's heard only.
- The master's meters, loudness and a Summing track (0052) take the master
  before the Cue track: what they read is what's exported.

## Consequences

The metronome still goes straight out, not through the Cue track.
Tested: live and exported, a click through a Cue track turned down 12 dB,
panned left and mono is heard that way and exported unchanged.
