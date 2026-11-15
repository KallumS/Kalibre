# 0017. Retrospective recording

## Context

The best take is often the one played before pressing Record.

## Decision

Every MIDI message from a keyboard is kept with when it came and where the song was. Capture MIDI (Shift-Cmd-R) turns the last performance (back to a pause of three seconds) into a clip: where it was played if the song was playing, otherwise at the cursor. An armed or monitored track keeps its last minute of input; Capture Audio puts the last 30 seconds at the cursor.

## Consequences

Only armed or monitored tracks keep audio, to keep memory down.
