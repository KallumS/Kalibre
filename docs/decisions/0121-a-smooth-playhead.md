# 0121. A smooth playhead

## Context

Reported (22 November): the playhead moves jerkily while playing. Its place
was the engine's position, which moves once an audio block (10 to 20 ms),
read by a 30-a-second timer that beats against the blocks and the screen, and
drawn at whole ticks.

## Decision

- The audio thread notes when each block it plays began and where
  (`Engine::smoothPosition`, read without a lock through a sequence count);
  the playhead is that place plus the time since, at most a quarter of a
  second on (if the audio stops coming, so does the playhead), wrapped at
  the loop's end, less the graph's delay as before.
- The arrangement and the piano roll move it on every refresh of the screen
  (`VBlankAttachment`), at its place between ticks
  (`Controller::playheadExact`), repainting only its strip.

## Consequences

Tested: between blocks the smooth place moves with the clock while the
block's place stands still, and stops a quarter of a second on with no
audio. How smooth it looks on the Mac's screen is to be seen there.
