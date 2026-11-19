# 0096. Automation lined up with plug-in delay compensation; every loop pass exact

## Context

Asked (19 November, night): is plug-in delay compensation sample accurate?

Checked by rendering. The sound was: a click through a 300-sample plug-in
landed on the very sample of the same click on a track without one (the
test had allowed a sample either way; it needed none). Automation wasn't:
the track's Source draws every lane for the song position it's playing
(0089), but the sound reaching the fader, the sends and any plug-in after
one that delays is later by that delay. A mute lane on a track through a
300-sample plug-in let the sound through 300 samples early. Asked to fix it.

## Decision

- **Each consumer reads its lane as late as its sound.** `Fader`,
  `SendGain` and `TimedPlugin` each carry an `automationDelay`, in samples.
  `Engine::lineUpAutomation` sets them after every graph rebuild
  (`ParallelGraph::onRebuilt`), from `ParallelGraph::latencyInto`: how late
  the sound reaching each node is, counted as the plan and JUCE's graph count
  it, less how late the track's Source plays. So a sidechain that makes the
  graph hold a plug-in's input back is counted too, and a plug-in whose
  latency changes is re-counted when the graph rebuilds for it.
- **The Source remembers where the song was.** A small fixed ring of spans
  (`TrackSource::spans`: from this sample on, the song was here) gets a new
  entry only where the song didn't carry straight on: a start, a locate, the
  loop's wrap. `secondsAt` reads it back for any sample up to 64 jumps ago;
  `ticksFor` turns a delay into the block's ticks for that consumer. No
  allocation on the audio thread; a delay of 0 uses the block's own ticks as
  before. A plug-in's block is cut where its delayed lane wraps or jumps, as
  it's cut at the loop's wrap.
- **The loop wraps exactly.** Testing round a loop showed passes a sample
  short now and then: the wrap was placed by rounding down, and a loop ending
  on a block's last sample wrapped one early. It now wraps on the first sample
  at or after the loop's end, on the next block's first sample when that's
  where it falls.

## Consequences

- Tested: a mute lane, a plug-in's lane (after the delaying plug-in and
  before it) and a send's lane, each on the very sample in an export with a
  300-sample plug-in on the track; played live round a one-second loop and a
  loop four blocks long, the mute lane under the sound across each wrap and
  every pass exactly as long as the loop. Each failed before the change.
- Not delayed: modulators (an LFO, an envelope, the follower), which move
  their targets once a block, as before. Live input monitored through a
  delaying plug-in is heard late, as in any DAW: that's the plug-in's delay,
  not compensation's.
