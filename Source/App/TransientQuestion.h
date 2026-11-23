/*
    TransientQuestion - Split at Transients' question (decision 0130): how
    readily a hit is found (Sensitivity: higher finds more, the quiet ones
    too), with how many clips it makes, said as the slider moves. Asked
    inside the main window (0002); the sensitivity chosen is kept for next
    time, and for the Sample Editor's hit marks and Quantise.
*/

#pragma once

#include "Controller.h"

namespace kal
{

// What's to be split: clips already in the song, or a file coming in (as one clip, all of it).
struct TransientSource
{
    Id clip = 0;
    juce::File file;
};

void askTransients (Controller&, const juce::String& title, std::vector<TransientSource>, std::function<void (double sensitivity)> go);

} // namespace kal
