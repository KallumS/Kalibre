/*
    Keys - every key the window answers, and which key does what (decision 0045).

    Each command has a name and a default key; any can be given another in
    the browser's Keys tab (click it, press the new key). A key given to one
    command is taken from whichever had it. The choices are kept with the
    app, in Application Support/Kalibre/Keys.json, and the menus show them.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace kal
{

class Keys
{
public:
    struct Command
    {
        juce::String id, name;
        juce::KeyPress defaultKey, key;
        std::function<void()> run;
    };

    void add (const juce::String& id, const juce::String& name, juce::KeyPress defaultKey, std::function<void()> run);
    bool handle (const juce::KeyPress&);                 // runs the command with this key, if any
    juce::KeyPress keyFor (const juce::String& id) const;
    juce::String textFor (const juce::String& id) const; // for the menus: "⌘T", "R"
    void set (const juce::String& id, const juce::KeyPress&);
    void reset (const juce::String& id);
    void resetAll();
    std::vector<Command>& all() { return commands; }

    juce::File file;   // where they're kept; empty: not kept
    void load();
    void save() const;
    std::function<void()> onChange;

private:
    std::vector<Command> commands;
};

} // namespace kal
