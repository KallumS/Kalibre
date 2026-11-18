# 0078. The audio on every performance core

## Context

The user (17 November): CPU seemed high with one or two plug-ins; was
Kalibre using Apple silicon's cores properly? It wasn't: JUCE's
AudioProcessorGraph plays every node one after another on the audio
device's thread, so two heavy plug-ins on two tracks took turns on one
core. Logic, Live and Bitwig play tracks side by side on all the cores.
Asked on 18 November to build it (and to leave sandboxing for now).

## Decision

- **`Engine/ParallelGraph`**, an AudioProcessorGraph whose playing is
  replaced. JUCE's graph still holds the nodes and connections, prepares
  them, and counts the latency; after every rebuild a **plan** is made on the
  message thread: the nodes in an order where each follows what feeds it,
  each with buffers of its own, and a delay line on each connection that
  plug-in delay compensation needs, exactly where JUCE's graph puts one
  (read from its source: each input delayed by the latest input's latency
  less its own; MIDI not delayed; a node that doesn't produce MIDI passes
  none on; bypass, suspension and the callback lock as JUCE does them).
- **Each block**, the audio thread and a few worker threads take nodes as
  soon as everything feeding them is done (counted with atomics; a fixed
  queue). The workers are real-time threads, one fewer than the Mac's
  performance cores (`hw.perflevel0.physicalcpu`), joined to the audio
  device's workgroup, so macOS treats them as audio and keeps them on the
  performance cores. Between blocks they wait on an atomic.
- **Nothing on the audio threads allocates or locks**: buffers and delay
  lines are made with the plan; a new plan is swapped in with an atomic
  pointer, and the old one kept until the audio thread is done with its
  block. A plan is used only with the sample rate, block size and settings
  it was made for; otherwise JUCE's graph plays the block, as before.
- **Nodes that share something** run one after another (`runsAlone`): the
  hardware insert, which adds to the interface's shared bus.
- After each new plan JUCE's graph plays one block, so it lets go of nodes
  removed from it (not when exporting: the plan's delay lines would lose
  what they hold). Like JUCE's own after a change, a new plan's delay lines
  start empty.
- The crash report's plug-in name (0073) is noted per thread.
- **Use Every Core (n)**, a toggle in the browser's Audio tab, on unless
  turned off; kept in Kalibre's settings. Off, everything is as before.

## Consequences

Tested: a song with plug-ins of different latencies on parallel paths, a
folder, a return fed by sends, an instrument, exported on every core and
on one: the same, sample for sample. Four tracks with a plug-in taking
1 ms a block: on four threads, about 2.5 times as fast (Linux, four
cores). Live playback through an audio device: played by the plan. Under
ThreadSanitizer (a build of the tests that reports any two threads touching
the same memory unsafely), the multi-core tests and the engine's tests ran
with no warnings.

Found on the way: an export's first block was handed to JUCE's graph,
losing what the plan's delay lines held for the second; exports don't
hand over now. And JUCE orders nodes with a remembered search that isn't
guaranteed to put every node after its inputs; the plan's order is.

What one plug-in costs isn't changed: a single heavy instrument still
needs one core, and the CPU figure is still how much of each buffer's time
the audio takes (now with the tracks side by side). Plug-ins that aren't
safe to run beside other instances on other threads would show here first;
Use Every Core turns it off.
