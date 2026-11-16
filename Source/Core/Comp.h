/*
    Comp - takes and comping (decision 0029).

    Every pass of a recording is a take, kept on the track's version. What
    plays is the comp: a list of stretches of time, each taken from one take.
    The comp's clips are made from those stretches (`rebuild`) and sit on the
    track like any others, marked with the take they came from; choosing a
    different take for a stretch (a swipe across its lane) remakes them.
    Where one take's stretch meets another's, both get a 5 ms fade so the join
    doesn't click.

    Looped recording: the recording is one long file, played straight through
    while the song went round the loop; `audioPasses` cuts it into a take per
    pass, and `midiPasses` does the same for MIDI.

    No JUCE in here.
*/

#pragma once

#include "Capture.h"
#include "Model.h"

namespace kal::comp
{

inline constexpr double joinFade = 0.005;   // seconds

// Adds a take; it is chosen for the time it covers (the newest take wins).
Id addTake (Project&, Id track, std::vector<Clip> clips, const std::string& name = {});

// Plays `take` over [from, to).
void choose (Project&, Id track, Id take, Tick from, Tick to);

void removeTake (Project&, Id track, Id take);

// Remakes the comp's clips from its stretches.
void rebuild (Project&, TrackVersion&);

// The comp's clips become ordinary clips; the takes go.
void flatten (Project&, Id track);

// The span a take covers.
std::pair<Tick, Tick> extent (const Take&);

// A looped audio recording cut into a clip per pass. `recording` is the clip
// as if there had been no loop: its start where recording began, the file
// from 0, `seconds` long.
std::vector<Clip> audioPasses (const Clip& recording, double seconds, const TempoMap&, Tick loopStart, Tick loopEnd);

// Recorded MIDI events split where the song went back round the loop.
std::vector<std::vector<capture::Event>> midiPasses (const std::vector<capture::Event>&);

} // namespace kal::comp
