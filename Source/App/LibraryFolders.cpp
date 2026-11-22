#include "LibraryFolders.h"

namespace kal
{
using namespace juce;

LibraryFoldersView::LibraryFoldersView (LibraryView& v) : library (v)
{
    setTitle ("Library Folders");
    list.setModel (this);
    list.setRowHeight (theme::px (30));
    list.setColour (ListBox::backgroundColourId, theme::sunken);
    list.setColour (ListBox::outlineColourId, theme::rule);
    list.setOutlineThickness (1);
    list.setTitle ("Folders");
    addAndMakeVisible (list);
    for (auto* b : { &add, &removeButton, &again }) addAndMakeVisible (b);
    add.onClick = [this] { library.chooseFolders ([this] { refresh(); }); };
    removeButton.onClick = [this] { remove (list.getSelectedRow()); };
    again.onClick = [this] { library.rescan(); };
    removeButton.setEnabled (false);
    startTimerHz (4);
}

LibraryFoldersView::~LibraryFoldersView() { list.setModel (nullptr); }

void LibraryFoldersView::refresh()
{
    list.updateContent();
    removeButton.setEnabled (list.getSelectedRow() >= 0);
    repaint();
}

void LibraryFoldersView::remove (int row)
{
    auto& s = library.settings();
    if (row < 0 || row >= (int) s.folders.size()) return;
    const auto gone = s.folders[(size_t) row];
    s.folders.erase (s.folders.begin() + row);
    s.open.erase (gone);
    library.settingsChanged();
    list.deselectAllRows();
    refresh();
}

int LibraryFoldersView::getNumRows() { return (int) library.settings().folders.size(); }

void LibraryFoldersView::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    const auto& folders = library.settings().folders;
    if (row < 0 || row >= (int) folders.size()) return;
    if (selected) { g.setColour (theme::rule); g.fillRect (0, 0, w, h); }
    const File f (folders[(size_t) row]);
    g.setColour (f.isDirectory() ? theme::text : theme::textSecondary);
    g.setFont (theme::font());
    g.drawText (f.getFullPathName() + (f.isDirectory() ? String() : String ("  (not found)")), 10, 0, w - 20, h, Justification::centredLeft, true);
}

void LibraryFoldersView::paint (Graphics& g)
{
    g.fillAll (theme::ground);
    g.setColour (theme::text);
    g.setFont (theme::font());
    auto r = getLocalBounds().reduced (theme::px (16));
    g.drawFittedText ("Kalibre looks through these folders, and every folder inside them, for samples and MIDI files to list in the browser's "
                      "Sounds tab. Nothing else on this " + String (ui::words().computer) + " is looked through.",
                      r.removeFromTop (theme::px (40)), Justification::topLeft, 3);
    g.setColour (theme::textSecondary);
    g.setFont (theme::font (theme::smallPoints));
    const auto& sc = library.scanner();
    const String state = sc.scanning() ? (sc.filesToRead() > 0 ? "Reading new sounds: " + String (sc.filesRead()) + " of " + String (sc.filesToRead())
                                                               : "Checking " + String (sc.filesFound()) + " sounds for changes")
                                       : String ((int) sc.index().items.size()) + " sounds found";
    g.drawText (state, again.getRight() + theme::px (12), again.getY(), getWidth() - again.getRight() - theme::px (28), again.getHeight(), Justification::centredLeft, true);
}

void LibraryFoldersView::resized()
{
    auto r = getLocalBounds().reduced (theme::px (16));
    r.removeFromTop (theme::px (48));
    auto buttons = r.removeFromBottom (theme::px (28));
    r.removeFromBottom (theme::px (10));
    list.setBounds (r);
    for (auto* b : { &add, &removeButton, &again })
    {
        b->setBounds (buttons.removeFromLeft (b->idealWidth()));
        buttons.removeFromLeft (theme::px (8));
    }
}

LibraryFoldersWindow::LibraryFoldersWindow (LibraryView& v)
    : DocumentWindow ("Library Folders", theme::ground, DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new LibraryFoldersView (v), false);
    setResizable (true, false);
    setResizeLimits (480, 300, 10000, 10000);
    setSize (640, 380);
}

bool LibraryFoldersWindow::keyPressed (const KeyPress& k)
{
    if (k == KeyPress ('w', ModifierKeys::commandModifier, 0) || k.getKeyCode() == KeyPress::escapeKey)
    {
        setVisible (false);
        return true;
    }
    return DocumentWindow::keyPressed (k);
}

} // namespace kal
