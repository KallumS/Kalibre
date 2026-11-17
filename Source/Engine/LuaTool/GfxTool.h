/*
    GfxTool - a tool drawn with REAPER's gfx rather than ReaImGui
    (decision 0068): Orchestration Helper, its script and its two databases
    unchanged, run by an embedded Lua and drawn with JUCE into the device's
    window.

    Adapted from the stand-in for REAPER the repo's Mac app carries
    (macos/host.c, macos/mac.m): the same gfx and reaper calls, with the
    same meanings, but one per device instead of one per program, its
    window a picture JUCE draws, its menus JUCE's, its settings kept with the
    project.
*/

#pragma once

#include "LuaTool.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

struct lua_State;

namespace kal::tools
{

class GfxProcessor;

class GfxSession
{
public:
    explicit GfxSession (GfxProcessor&);
    ~GfxSession();

    // One frame of the script on a surface this many pixels across, at this
    // density; `menuOver` is the window a menu opens over.
    void frame (int pixelWidth, int pixelHeight, double scale, juce::Component* menuOver);
    const juce::Image& picture() const { return image; }
    const juce::String& error() const { return failure; }
    juce::String run (const juce::String& lua);

    // input, in pixels (gfx's own: y down)
    struct Input { double x = -10000, y = -10000; int buttons = 0, mods = 0; double wheel = 0; } input;
    void key (int code, int mods);

    int preferredWidth = 760, preferredHeight = 680;

    GfxProcessor& processor;

    // what the gfx calls reach (GfxTool.cpp)
    struct Font { juce::Font font { juce::FontOptions {} }; juce::String face; };
    std::map<int, Font> fonts;
    int currentFont = 1;
    bool needsClear = true, inFrame = false, windowOpen = false, wantRetina = false;
    juce::Image image;
    std::unique_ptr<juce::Graphics> g;
    double scale = 1;
    juce::Component* menuParent = nullptr;
    struct Key { int code, mods; };
    std::vector<Key> keys;
    size_t keyRead = 0;
    juce::String failure;
    lua_State* L = nullptr;
    int deferredRef = -2, atexitRef = -2;   // LUA_NOREF

    double getnum (const char* field, double fallback);
    void setnum (const char* field, double v);
    void clearIfNeeded();
    void refreshSize();
    bool call (int nargs, int nresults);
};

class GfxProcessor : public juce::AudioProcessor
{
public:
    explicit GfxProcessor (const LuaToolSpec&);
    ~GfxProcessor() override;

    const LuaToolSpec& spec;
    GfxSession* session() { return current.get(); }

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l.getMainInputChannelSet() == l.getMainOutputChannelSet(); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}   // all of it passes through
    using juce::AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return spec.name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    std::map<juce::String, juce::String> ext;   // REAPER's ExtState, kept with the project
    int editorWidth = 0, editorHeight = 0;

private:
    std::unique_ptr<GfxSession> current;
};

} // namespace kal::tools
