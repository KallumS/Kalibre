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

// "4 clips: one for each of the 3 hits, and one for what comes before the
// first", or "4 clips, one for each hit": the clips counted as the Sample
// Editor counts the hits (the UAT).
juce::String clipsForHits (int clips, int hits);

void askTransients (Controller&, const juce::String& title, std::vector<TransientSource>, std::function<void (double sensitivity)> go);

} // namespace kal
