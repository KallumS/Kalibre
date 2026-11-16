# 0038. Trim, automation copy and paste, mute and send lanes; RMS, CPU and dropouts

## Decision

- **Trim** mode: moving a control while playing moves the lane by the hand's change from what the lane said when the touch began, keeping its shape (`edit::writeTrim`).
- **Mix moves are written** in Touch, Latch, Write and Trim: the mixer's faders, pan knobs, mute buttons and send knobs, like plug-in parameters. Latch and Write hold until Stop.
- **Copy and paste** on an automation lane: the loop range (or the bar clicked) is copied and pasted where you right-click.
- **Mute and send lanes play**: the fader and the send nodes follow them.
- **RMS**: each meter shows the 300 ms average as a light line across the peak bar.
- **CPU per track**: every plug-in in the graph is wrapped in `TimedPlugin`, which times its processing and passes everything else through (buses, latency, bypass, ARA and VST3 extensions). `Engine::instance` hands out the plug-in itself. The mixer shows each track's share of real time.
- **Dropouts**: a block that took longer than it lasts, plus the interface's own count; shown in the top bar (click to reset). Changed 16 November 2026, by the user: the top bar's CPU shows only a percentage; dropouts are still counted, not shown there.

## Consequences

- The wrapper is one more layer for every plug-in; it adds no latency and copies nothing.
