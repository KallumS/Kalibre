/*
    ToolSession - one run of a tool's window script (decision 0068): its Lua
    state, its ImGui context, and the stand-ins for REAPER it calls
    (`reaper`, `host`, `dofile` of its own files, carried inside Kalibre in
    Engines/tools/). Lives on the message thread. A loaded project starts a
    new one, so the script reads its settings afresh, as when REAPER opens it.

    From the Starting Blocks plugin branch (plugin/source/Session.cpp). What
    differs: the scripts come from Kalibre's embedded engines, one folder per
    tool, with the place stand-ins (sb_place.lua, mc_place.lua) shared beside
    them; `host.insert` puts a block into the song; and the window belongs to
    Kalibre, so a script that closes itself (Esc) keeps running.
*/

#pragma once

#include "ImGuiLua.h"

#include <juce_gui_basics/juce_gui_basics.h>

struct ImGuiContext;
struct ImDrawData;

namespace kal::tools
{

class ToolProcessor;

class ToolSession
{
public:
    explicit ToolSession (ToolProcessor&);
    ~ToolSession();

    // One frame of the script at this size (points) and density.
    void frame (float width, float height, float scale);

    ImDrawData* drawData() const;
    const juce::Image& atlas() const { return fontAtlas; }
    const juce::String& error() const { return failure; }

    // input, in points (between frames)
    void mouseMove (float x, float y);
    void mouseButton (int button, bool down);
    void mouseWheel (float dx, float dy);
    void modifiers (const juce::ModifierKeys&);
    void key (int imguiKey, bool down);
    void character (juce::juce_wchar);
    void focus (bool has);
    bool wantsKeyboard() const;

    // A chunk of Lua run in the script's state: what it returns, as text (tests).
    juce::String run (const juce::String& lua);

    float minWidth() const { return state.minW; }
    float minHeight() const { return state.minH; }

    ToolProcessor& processor;

private:
    void use();
    void buildFonts (float scale);

    lua_State* L = nullptr;
    ImGuiContext* context = nullptr;
    ImGuiLuaState state;
    float fontScale = 0;
    juce::Image fontAtlas;
    juce::String failure;
};

// Draws a finished ImGui frame with JUCE.
void drawImGui (juce::Graphics&, ImDrawData*, const juce::Image& atlas);

} // namespace kal::tools
