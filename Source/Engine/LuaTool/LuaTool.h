/*
    LuaTool - the family's ReaScripts, built into Kalibre as MIDI devices
    (decision 0068): Starting Blocks, Starting Blocks Notation and Midi
    Catalogue, each its own window script, unchanged, run by an embedded Lua
    on a stand-in for ReaImGui (ImGuiLua.cpp) and drawn with JUCE; and
    Orchestration Helper, drawn with REAPER's gfx (GfxTool.cpp), a reference
    beside the track that passes everything through.

    Adapted from the plug-in the Starting Blocks repos' plugin branches build
    (plugin/source/Plugin.cpp): the script builds a block; the device plays
    it out of its MIDI output, as an audition or over and over with the song,
    to the instrument after it; Insert at Cursor puts it on the device's
    track at the playhead (several parts: a new track each), as one undo step.
    What differs: the block reaches the audio thread through a Swap, not a
    lock; the tool is a ToolSpec, not a build of its own.
*/

#pragma once

#include "Model.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct lua_State;

namespace kal
{

struct LuaToolSpec
{
    const char* id;        // "starting-blocks": the identifier is "tool:" + this
    const char* name;      // "Starting Blocks"
    const char* script;    // Engines/tools/<id>/<script>
    const char* about;
    bool gfx = false;      // drawn with gfx (GfxTool), not ReaImGui
    // More of `reaper` for this tool, added to its table (on the stack) before the
    // script runs: DAW Battle Pass's, answered from the song (App/BattlePass.cpp, 0111).
    void (*extendReaper) (lua_State*) = nullptr;
};

// DAW Battle Pass (decision 0111): a window script like the others, but never a
// device; the app runs it (App/BattlePass.h) and sets its extendReaper.
LuaToolSpec& battlePassSpec();

const std::vector<LuaToolSpec>& luaTools();
PluginRef luaToolRef (const LuaToolSpec&);

// The tool a reference names, made; nullptr if it names none.
std::unique_ptr<juce::AudioPluginInstance> makeLuaTool (const PluginRef&);

// A block a tool hands to the song: one part per track, notes in quarter
// notes from its start.
struct ToolBlock
{
    struct Note { double start = 0, length = 0.25; int pitch = 60, velocity = 100, channel = 0; };
    struct Part { std::string name; std::vector<Note> notes; };
    std::string name;
    double beats = 0;
    std::vector<Part> parts;
};

// Set by the app (the Controller): puts a tool's block into the song, given
// the device instance it came from. Message thread. False if it couldn't.
extern std::function<bool (juce::AudioPluginInstance&, const ToolBlock&)> insertToolBlock;

// For the tests: the error the tool's script stopped with ("" while it runs),
// and a line of Lua run in its state (what it returns, as text).
juce::String luaToolError (juce::AudioPluginInstance&);
juce::String luaToolRun (juce::AudioPluginInstance&, const juce::String& lua);

} // namespace kal
