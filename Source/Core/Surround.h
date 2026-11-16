/*
    Surround - where a track sits among the speakers (decision 0040).

    The mix can be stereo, quad, 5.1 or 7.1 (channels in the order WAV files
    and Core Audio use: L R C LFE, then the surrounds). A track is placed by
    its pan (left to right) and its front-back position; it's played by the
    two speakers either side of that direction, at constant power. The LFE
    channel only gets what's sent to it.

    No JUCE in here.
*/

#pragma once

#include "Model.h"

#include <array>

namespace kal::surround
{

int channels (SurroundFormat);
const char* name (SurroundFormat);

// Gains for each output channel (unused ones 0) for a source at pan
// (-1 left .. 1 right) and frontBack (-1 behind .. 1 in front).
std::array<float, 8> gains (SurroundFormat, double pan, double frontBack);

} // namespace kal::surround
