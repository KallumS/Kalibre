/*
    PanLaw - how Kalibre pans (the audio engine's document, decision 0081):
    constant power with sine curves; 0 dB in the middle, exactly, so a track
    panned to the centre is untouched; +3 dB fully to one side, with nothing
    on the other.
*/

#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <utility>

namespace kal
{

inline std::pair<float, float> panGains (float pan)
{
    if (pan == 0.0f) return { 1.0f, 1.0f };
    if (pan <= -1.0f) return { juce::MathConstants<float>::sqrt2, 0.0f };
    if (pan >= 1.0f) return { 0.0f, juce::MathConstants<float>::sqrt2 };
    const double angle = ((double) pan + 1.0) * juce::MathConstants<double>::pi * 0.25;
    return { (float) (std::sqrt (2.0) * std::cos (angle)), (float) (std::sqrt (2.0) * std::sin (angle)) };
}

} // namespace kal
