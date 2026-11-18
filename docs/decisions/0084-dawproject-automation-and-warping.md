# 0084. DAWproject automation and warping

## Context

DAWproject (0077) carried no automation, and audio came in unwarped.
Asked (18 November) to carry both.

## Decision

- **Automation**, written as Points in each track's lanes of the
  arrangement, aimed at the parameter they move: a track's (folders',
  returns' and the master's too) volume (as gain), pan, mute (on and off),
  each send's level, and plug-ins' parameters (each automated one listed in
  its device's Parameters, by index and name; the X/Y Morph's X and Y as its
  first two). Points are linear or held; read back the same. Another
  program's automation of something Kalibre hasn't (a MIDI controller on a
  track) is left out, and the message line says so.
- **Warping**: a warped audio clip's content is a Warps timeline in beats,
  each Warp a beat of the clip's content and the moment of the file then:
  its markers, or two from the file's own tempo. The warp mode as the
  Audio's algorithm: repitch, granular or stretch (spectral). A looped audio
  clip carries its loop. Read back as markers, in the mode named
  (anything else as spectral); a clip whose warps agree with the song's
  tempo plays unstretched (0081).
- The written file is checked with xmllint against the official schema.

## Consequences

Tested: a song with every kind of lane and two warped clips written and
read back, each point and marker the same. Automation curves (a lane's
bend) aren't in DAWproject: they come back straight. Not yet tried with
Bitwig, Studio One or Cubase themselves.
