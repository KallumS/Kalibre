# 0097. Exports: Record the Tail, Export the Second Pass; every export starts clean

## Context

Asked (19 November, night): when exporting audio, an option to export the
second pass and an option to record the tail.

Before, every export played the song on for two seconds past the end: past
the song's end that's its tail, but past a loop range's end it was the next
bars of the song, not the loop ringing out. And there was no way to hear the
end of a loop wrap into its start.

## Decision

- **Record the Tail** (`RenderOptions::ringOut`). The range is played to its
  very sample (the last block cut short there); from then the transport is
  stopped for the song (`clock.playing` false: no new notes, held notes let
  go, clips silent) while every plug-in goes on running. The render goes on
  until the output has been under -96 dBFS for a quarter of a second, and
  the file is cut on its last sample above that; at most 30 seconds. Off,
  the file ends on the range's end, sample-exact. On by default (exports had
  always had a tail).
- **Export the Second Pass** (`RenderOptions::secondPass`). The range is
  played once unheard, wrapping back to its start on the exact sample as the
  loop does live (`clock.wrapAt`, the Source stopping notes there as it does
  round a loop), then exported the second time through: what rings past the
  end is in the file's start. Exactly the range's length (and the tail after,
  with both).
- **Asked before each export of audio** (Export the Mix, at a Loudness,
  Export Every Track), as a question over the window (`ui::present`), and as
  two more ticks in Export Several at Once. Remembered in the window's
  settings (`exportTail`, `exportSecondPass`).
- **Every export starts clean.** Testing the second pass showed the first
  pass's sound already at the start of an ordinary export: Kalibre Synth's
  voices were still releasing from the export before (the difference between
  two renders noticed on 19 November). The graph is now reset before every
  export (`ParallelGraph::reset`: each node's `reset`, and the plan's delay
  lines emptied); Kalibre Synth, Sampler and Trombolese clear their voices
  in `reset`, and `TimedPlugin` passes it on and empties its bypass delay.
  Third-party plug-ins clear what they hold in their own `reset`.
- `RenderOptions::tail` (the song played on for so long) stays for freezing
  and bouncing; the export questions set it to 0.

## Consequences

- Tested: a release ringing past the end and the file ending as it dies
  away, a note after the end not played; exactly the range with the tail off;
  the second pass with the release from the end at its start, silent where
  the first pass was, the note where it was, exactly the range long; both
  together; one engine exporting the same song twice, sample for sample the
  same (failed before the reset); the question with its two ticks.
- A plug-in that never falls silent (noise, a drone) makes a tail 30 seconds
  long.
- The ticks were drawn with their names twice (above and beside the box) in
  Export Several at Once too; both questions now show each once.
