/*
    KeysPanel - the Keys window, Help > Keys... (decision 0045): every command and its
    key. Click one, then press the key it should have; Esc leaves it as it
    was, Delete takes its key away.
*/

#pragma once

#include "Controller.h"
#include "Keys.h"
#include "Theme.h"

namespace kal
{

class KeysPanel : public juce::Component, private juce::ListBoxModel
{
public:
    KeysPanel (Keys&);
    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void refresh();

private:
    Keys& keys;
    juce::ListBox list;
    theme::HouseButton resetOne { "Default Key", "Give the chosen command its usual key back." };
    theme::HouseButton resetAll { "All Defaults", "Every command back to its usual key." };
    int capturing = -1;

    int getNumRows() override { return (int) keys.all().size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
};

} // namespace kal
