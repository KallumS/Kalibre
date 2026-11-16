# 0024. Automation picks up the last touched control

## Context

Finding a parameter in a list is slow.

## Decision

Kalibre listens to every plug-in's parameters. The last one moved by a hand is remembered; Automation > Automate (its name) makes its lane, and Modulate gives it a modulator. With a track in Touch, Latch or Write, moving a control while playing writes automation.

## Consequences

Values set by automation playback don't count as touched.
