/*
    Articulations - reading the articulation maps other programs make
    (decision 0072), as Note Position Fixer reads them (its npvf_maps.lua):

    - Cubase, Nuendo and Dorico expression maps (.expressionmap), which is
      what BabylonWaves Art Conductor makes for Cubase;
    - Logic articulation sets (.plist), Art Conductor's for Logic;
    - Reaticulate banks (.reabank), REAPER's articulation system.

    Each articulation's outputs (keyswitches with their velocities,
    controllers, program changes) and its channel become a Kalibre
    articulation: the first key, controller and program in their places, any
    more in `more`. Only the outputs are read, not Cubase's note changer,
    remote triggers or Reaticulate's routing.

    No JUCE in here.
*/

#pragma once

#include "Model.h"

#include <string>
#include <vector>

namespace kal::articulations
{

struct Imported
{
    std::vector<ArticulationMap> maps;   // ids left 0: given when they join a project
    std::string format;                  // "Cubase expression map", "Logic articulation set", "Reaticulate bank"
    std::string error;                   // why nothing was read
};

// Reads any of the three, by what the text holds (the file's name is a hint).
Imported import (const std::string& text, const std::string& filename);

// In words: "C0 + D3 vel 100 + CC20 = 64, channel 3".
std::string describe (const Articulation&);

// The library of maps kept between songs, as JSON.
std::string saveLibrary (const std::vector<ArticulationMap>&);
std::vector<ArticulationMap> loadLibrary (const std::string& text);

} // namespace kal::articulations
