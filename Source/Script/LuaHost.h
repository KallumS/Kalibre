/*
    LuaHost - user scripts (decision 0019).

    Scripts are Lua 5.4, as REAPER's are, so the family's habits carry over.
    A script sees the project through the `kalibre` table - tracks, clips,
    notes, tempo, scale, the selection - and changes it with the same edit
    functions the window uses. A whole script run is one undo step, and a
    script that runs away is stopped after a few seconds.

    Scripts live in ~/Library/Application Support/Kalibre/Scripts and appear
    in the Scripts menu; any script can also be bound to a key. The API is in
    docs/SCRIPTING.md.

    No JUCE in here.
*/

#pragma once

#include "Model.h"

#include <functional>
#include <set>
#include <string>

namespace kal::script
{

struct Context
{
    std::vector<Id> selectedTracks;
    Id selectedClip = 0;
    std::set<Id> selectedNotes;
    Tick cursor = 0;
    Tick playhead = 0;

    std::function<void (const std::string&)> message;          // shown in the window's message line
    std::function<bool (const std::string&)> action;           // run one of the window's commands by name
};

struct Result
{
    bool ok = true;
    bool changed = false;          // the project was changed
    std::string error;
    std::string printed;           // what print() wrote
    Context after;                 // the selection as the script left it
};

// Runs `code` against `project`. `name` is shown in errors.
Result run (Project& project, const std::string& code, const std::string& name, Context context = {},
            double timeLimitSeconds = 5.0);

} // namespace kal::script
