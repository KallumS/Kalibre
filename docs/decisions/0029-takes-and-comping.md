# 0029. Takes and comping

## Context

Recording the same part several times and keeping the best of each pass.

## Decision

A track's version holds takes (one per pass of recording) and a comp: stretches of time, each played from one take. The comp's clips are made from the stretches (`comp::rebuild`) and marked with their take; joins between takes get a 5 ms fade. Recording round a loop makes a take per pass, for audio and MIDI; recording audio over audio turns what was there into the first take. The newest take plays. Take lanes open under the track (track menu > Show Takes); drag across a take's lane to use it there, click one of its clips to use all of that clip. A take lane's menu deletes the take, uses it everywhere, or flattens the comp into ordinary clips.

## Consequences

- Editing a comp clip (moving, trimming) holds until the comp is changed again, which remakes its clips from the stretches.
- Recording MIDI without a loop makes an ordinary clip, as before: MIDI overdubs layer rather than replace.
