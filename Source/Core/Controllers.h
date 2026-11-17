/*
    Controllers - the MIDI controllers' standard names (MIDI 1.0), as every
    DAW lists them, for the CC lanes (decision 0060).

    No JUCE in here.
*/

#pragma once

#include <string>
#include <vector>

namespace kal::controllers
{

// "Modulation", "Expression", "Sustain Pedal"; "Undefined" for the numbers
// the standard leaves free (CC 21, which AutoCC uses for vibrato, is one).
std::string name (int cc);

// "CC 7 Volume"; "CC 21" for an undefined one.
std::string label (int cc);

// The ones most often drawn, listed first in a menu.
const std::vector<int>& common();

} // namespace kal::controllers
