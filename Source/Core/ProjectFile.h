/*
    ProjectFile - a project as JSON (.kalibre), and back.

    Every field has a default, so a file from an older Kalibre opens in a
    newer one with what it lacks filled in, and unknown fields are ignored.
    Plug-in states are the plug-ins' own bytes in base64, so a project opens
    exactly as it was saved even with a plug-in deactivated.

    No JUCE in here.
*/

#pragma once

#include "Model.h"

#include <optional>
#include <string>

namespace kal::file
{

inline constexpr int formatVersion = 1;

std::string save (const Project&);
// Nothing when the text isn't a Kalibre project. `error` says why.
std::optional<Project> load (const std::string& text, std::string* error = nullptr);

// Articulation maps alone, for the Articulation Manager's library (decision 0072).
std::string saveMaps (const std::vector<ArticulationMap>&);
std::vector<ArticulationMap> loadMaps (const std::string& text);

} // namespace kal::file
