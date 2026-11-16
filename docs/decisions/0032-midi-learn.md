# 0032. MIDI learn

## Context

A knob on a keyboard should be able to move any control.

## Decision

Touch a control (a plug-in's parameter, or a track's volume, pan or mute in the mixer), choose Automation > MIDI Learn (Cmd-L), move a knob, fader or button: from then on it moves that control. Mappings are a channel and controller number (pitch bend counts, for faders that send it), are saved with the project, and are listed under Automation > MIDI Mappings to forget. Every MIDI message now reaches Kalibre before the tracks: learned controllers are taken out so they don't play into an instrument. A mapped plug-in parameter moves as if by hand, so it writes automation in Touch, Latch and Write.

## Consequences

Learning a controller that's already mapped, or a control that already has one, replaces the old mapping.
