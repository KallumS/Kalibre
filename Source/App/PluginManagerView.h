/*
    PluginManagerView - Kalibre > Plug-in Manager... (decision 0047), in a
    window of its own: settings may have windows (decision 0002, amended).

    Every plug-in found, and every one turned off: those that crashed, hung or
    wouldn't open while being scanned, and those turned off by hand. Search,
    sort by any column, show only the ones turned off; scan for new ones, scan
    the chosen ones again, turn them off or on, show their files in the Finder.
    Right-click files the chosen ones as MIDI effects, instruments or audio
    effects (decision 0069).
*/

#pragma once

#include "Controller.h"
#include "Theme.h"

namespace kal
{

class PluginManagerView : public juce::Component, private juce::ListBoxModel, private juce::ChangeListener
{
public:
    explicit PluginManagerView (Controller&);
    ~PluginManagerView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh();

    struct Row
    {
        juce::String name, maker, format, kind, file;
        bool on = true;
        PluginManager::Reason reason = PluginManager::Reason::Crashed;
        juce::String detail;
        juce::PluginDescription desc;   // empty for a file turned off before it was read
    };
    const std::vector<Row>& shownRows() const { return rows; }

private:
    Controller& c;
    juce::ListBox list;
    juce::TextEditor search;
    theme::HouseButton scanNew { "Scan for New Plug-ins", "Look for plug-ins installed since the last scan. Each is opened in a helper, so one that crashes can't close Kalibre." };
    theme::HouseButton scanAgain { "Scan Again", "Look at the chosen plug-ins again, as if new: for one that crashed before and has been updated since." };
    theme::HouseButton turnOff { "Turn Off", "Leave the chosen plug-ins out of the browser and the menus, and never load them." };
    theme::HouseButton turnOn { "Turn On", "Turn the chosen plug-ins back on. One that crashed or hung is scanned again first, and stays off if it still does." };
    theme::HouseButton reveal { ui::words().showInFiles, "Show the chosen plug-in's file." };
    theme::HouseButton report { "Random Settings Report...", "Write every control of every plug-in to a file on your Desktop, each marked as one a plug-in's Random "
                                                             "changes or leaves alone, and why. Each plug-in is opened in a helper, so this takes a few minutes." };
    theme::HouseButton onlyOff { "Only Turned Off", "Show only the plug-ins that are turned off.", true };

    std::vector<Row> rows;
    int sortColumn = 0;
    bool sortUp = true;

    juce::Rectangle<int> headerArea, infoArea;
    std::vector<int> columnEdges (int width) const;
    std::vector<const Row*> chosen() const;
    void updateButtons();
    void showFileAsMenu();

    int getNumRows() override { return (int) rows.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    juce::SparseSet<int> selectionNow, selectionBefore;   // Option-click adds to the one before (decision 0053)
    juce::String getNameForRow (int row) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};

// The window it's shown in: closed with its close button, Command-W or Esc.
class PluginManagerWindow : public juce::DocumentWindow
{
public:
    explicit PluginManagerWindow (Controller&);
    void closeButtonPressed() override { setVisible (false); }
    bool keyPressed (const juce::KeyPress&) override;
};

} // namespace kal
