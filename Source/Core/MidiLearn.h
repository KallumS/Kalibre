/*
    MidiLearn - knobs and faders on a MIDI controller learned onto controls
    (decision 0032).

    Choose Automation > MIDI Learn with a control last touched (a plug-in's
    parameter, a track's volume, pan or mute), move a knob, fader or button on
    the controller, and from then on it moves that control. A mapping is a
    channel and a controller number (pitch bend counts as controller 128, for
    controllers whose faders send it). Mapped messages are kept from reaching
    the tracks, so a knob doesn't also play into an instrument.

    No JUCE in here.
*/

#pragma once

#include "Model.h"

#include <cstdint>

namespace kal::midimap
{

inline constexpr int pitchBend = 128;

// What a message would be learned as: false for anything that isn't a
// controller or pitch bend.
bool keyOf (const std::uint8_t bytes[3], int& channel, int& controller);

// The value a message carries, 0 to 1.
double normalised (const std::uint8_t bytes[3]);

// The mapping a message moves, or -1. A mapping on channel 0 hears every channel.
int match (const std::vector<MidiMapping>&, const std::uint8_t bytes[3]);

// A track's volume from 0-1, on the same scale as volume automation (-60 to +6 dB).
inline double volumeFromNormal (double v) { return v * 66.0 - 60.0; }
inline double panFromNormal (double v) { return v * 2.0 - 1.0; }

// Learns a mapping (replacing any on the same controller or the same control).
void learn (Project&, int channel, int controller, Id track, const AutoTarget&, const std::string& name);

} // namespace kal::midimap
