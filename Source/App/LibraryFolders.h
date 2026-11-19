/*
    LibraryFolders - Kalibre > Library Folders...: the folders the browser's
    Sounds tab looks in (decision 0094). A settings window of its own, as the
    Plug-in Manager is (0002 amended): add a folder, take one out, look
    through them again.
*/

#pragma once

#include "LibraryView.h"

namespace kal
{

class LibraryFoldersView : public juce::Component, private juce::ListBoxModel, private juce::Timer
{
public:
    explicit LibraryFoldersView (LibraryView&);
    ~LibraryFoldersView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    void remove (int row);   // tests

private:
    LibraryView& library;
    juce::ListBox list;
    theme::HouseButton add { juce::String::fromUTF8 ("Add Folder\xe2\x80\xa6"), "Choose another folder to look in for sounds." };
    theme::HouseButton removeButton { "Remove", "Stop looking in the chosen folder. Its files aren't touched." };
    theme::HouseButton again { "Look Again", "Look through every folder again for sounds added or changed since." };

    int getNumRows() override;
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
    void selectedRowsChanged (int) override { removeButton.setEnabled (list.getSelectedRow() >= 0); }
    void timerCallback() override { repaint(); }
};

class LibraryFoldersWindow : public juce::DocumentWindow
{
public:
    explicit LibraryFoldersWindow (LibraryView&);
    void closeButtonPressed() override { setVisible (false); }
    bool keyPressed (const juce::KeyPress&) override;
};

} // namespace kal
