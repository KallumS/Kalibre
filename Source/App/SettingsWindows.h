/*
    SettingsWindows - the studio and the keys, each a settings window of its
    own (0002 amended, as the Plug-in Manager and Library Folders are). They
    were tabs of the browser; the musician asked for the keys under Help only
    (Help > Keys...) and the studio in the File menu (File > Studio...).

    StudioWindow: sync, control surfaces, OSC and AI agents (StudioPanel,
    decisions 0034, 0035, 0043, 0099). KeysWindow: every command and its key
    (KeysPanel, 0045). Closing one hides it; Cmd+W and Esc too.
*/

#pragma once

#include "KeysPanel.h"
#include "StudioPanel.h"

namespace kal
{

class SettingsWindow : public juce::DocumentWindow
{
public:
    SettingsWindow (const juce::String& title, juce::Component* content, int w, int h);
    void closeButtonPressed() override { setVisible (false); }
    bool keyPressed (const juce::KeyPress&) override;
};

class StudioWindow : public SettingsWindow
{
public:
    explicit StudioWindow (Controller&);
    void showAgents();   // scrolled to AI Agents (Kalibre > AI Agents..., File > Studio > AI Agents)
    StudioPanel& panel() { return *studio; }

private:
    struct View;
    StudioPanel* studio = nullptr;
    View* view = nullptr;
};

class KeysWindow : public SettingsWindow
{
public:
    explicit KeysWindow (Keys&);
    KeysPanel& panel() { return *keys; }
    void refresh() { keys->refresh(); }

private:
    KeysPanel* keys = nullptr;
};

} // namespace kal
