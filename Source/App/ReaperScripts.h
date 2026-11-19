/*
    ReaperScripts - the family's REAPER scripts that hand a MIDI phrase to a
    JSFX, run unchanged inside Kalibre (decision 0093): Midular's importer
    and MIDI Production Center's loaders.

    Each runs in a Lua of its own with a `reaper` that answers as REAPER
    would about this song: its tracks, their devices and parameters (a
    JSFX's in its sliders' own units), the chosen MIDI clips as the selected
    items and their notes, the tempo map and markers, gmem (the JSFX's
    shared memory, through ysfx), defer, and messages into the message line.
    What they would ask for (a file, a name) is answered "cancelled":
    Kalibre loads from chosen clips.

    MIDI Production Center's background loader runs while the song has one,
    so its Load Item button works as it does in REAPER with the loader on.
*/

#pragma once

#include "Model.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

struct lua_State;

namespace kal
{

class Controller;

class ReaperScript : private juce::Timer
{
public:
    // `path`: the script among the embedded ones ("scripts/midular/midular_import.lua").
    // `focus`: the device it was run from, REAPER's focused FX; 0 for none.
    ReaperScript (Controller&, const juce::String& path, Id focus);
    ~ReaperScript() override;

    bool start();                         // runs it; false (and `error`) if it failed
    bool waiting() const { return ! deferred.empty(); }   // deferred work still to do
    void step();                          // one round of deferred calls (REAPER's ~30 a second)
    juce::String error;

    // For the `reaper` functions (ReaperScripts.cpp).
    Controller& c;
    Id focus = 0;
    juce::String path;
    juce::String gmemName;
    std::vector<int> deferred;            // Lua references to functions to call next
    std::vector<int> atExit;

private:
    lua_State* L = nullptr;
    void timerCallback() override { step(); }
    void fail (const juce::String&);
};

// The scripts a song needs running: the background loader while it has a MIDI
// Production Center; an import run from a device until it's done.
class ReaperScripts
{
public:
    explicit ReaperScripts (Controller& ctl) : c (ctl) {}
    void sync();                          // starts or stops the background loader
    // Loads the chosen MIDI clips into the device: Midular through its importer,
    // MIDI Production Center through its Load Selected MIDI Item script.
    bool loadChosenClips (Id device);
    ReaperScript* backgroundLoader() const { return loader.get(); }
    std::vector<std::unique_ptr<ReaperScript>> runs;

private:
    Controller& c;
    std::unique_ptr<ReaperScript> loader;
};

} // namespace kal
