#include "ArticulationManagerView.h"

#include "Articulations.h"

namespace kal
{
using namespace juce;

ArticulationManagerView::ArticulationManagerView (Controller& ctrl) : c (ctrl)
{
    setTitle ("Articulation Manager");
    maps.setModel (&mapsModel);
    arts.setModel (&artsModel);
    maps.setMultipleSelectionEnabled (true);
    maps.setTitle ("Articulation maps");
    arts.setTitle ("Articulations");
    for (auto* l : { &maps, &arts })
    {
        l->setRowHeight (theme::px (24));
        l->setColour (ListBox::backgroundColourId, theme::sunken);
        l->setColour (ListBox::outlineColourId, theme::rule);
        addAndMakeVisible (l);
    }
    for (auto* b : { &importButton, &useButton, &removeButton }) addAndMakeVisible (b);
    arts.setMultipleSelectionEnabled (true);
    delayLabel.setFont (theme::font (theme::smallPoints));
    delayLabel.setColour (Label::textColourId, theme::text);
    delayLabel.setJustificationType (Justification::centredRight);
    addAndMakeVisible (delayLabel);
    delayField.setInputRestrictions (6, "-0123456789.");
    delayField.setTooltip ("The chosen articulations' delay in milliseconds: how much later they play (negative: earlier), "
                           "for a library whose notes speak late. Kept in the library and in this song. Press Return to set it.");
    delayField.setTitle ("Delay of the chosen articulations, in milliseconds");
    delayField.setColour (TextEditor::backgroundColourId, theme::sunken);
    delayField.setColour (TextEditor::textColourId, theme::text);
    delayField.setColour (TextEditor::outlineColourId, theme::rule);
    delayField.setJustification (Justification::centredRight);
    delayField.onReturnKey = [this] { applyDelay(); };
    delayField.onFocusLost = [this] { applyDelay(); };
    addAndMakeVisible (delayField);
    importButton.onClick = [this]
    {
        chooser = std::make_unique<FileChooser> ("Import Articulation Maps", File::getSpecialLocation (File::userHomeDirectory), "*.expressionmap;*.plist;*.reabank");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                              [this] (const FileChooser& fc)
        {
            if (fc.getResults().isEmpty()) return;
            c.importArticulationMaps (fc.getResults());
            refresh();
        });
    };
    useButton.onClick = [this]
    {
        if (auto* r = chosenRow(); r && c.focus())
        {
            c.useArticulationMap (c.focusTrack, r->map);
            c.say ("The track " + String (c.focus()->name).quoted() + " uses " + String (r->map.name).quoted()
                   + ". Give notes articulations in the piano roll's articulation lane.", false, true);
            refresh();
        }
    };
    removeButton.onClick = [this]
    {
        auto library = c.articulationLibrary();
        int n = 0;
        for (int i = 0; i < (int) rows.size(); ++i)
            if (maps.isRowSelected (i) && rows[(size_t) i].inLibrary)
            {
                const auto name = rows[(size_t) i].map.name;
                library.erase (std::remove_if (library.begin(), library.end(), [&] (const ArticulationMap& m) { return m.name == name; }), library.end());
                ++n;
            }
        if (n == 0) return;
        c.saveArticulationLibrary (library);
        refresh();
    };
    c.addChangeListener (this);
    refresh();
}

ArticulationManagerView::~ArticulationManagerView() { c.removeChangeListener (this); }

void ArticulationManagerView::changeListenerCallback (ChangeBroadcaster*) { if (isShowing()) refresh(); }

void ArticulationManagerView::refresh()
{
    String keep;
    if (auto* r = chosenRow()) keep = r->map.name;
    rows.clear();
    for (auto& m : c.articulationLibrary()) rows.push_back ({ m, true, false });
    for (auto& m : c.project.articulationMaps)
    {
        bool found = false;
        for (auto& r : rows)
            if (r.map.name == m.name) { r.inSong = true; found = true; }
        if (! found) rows.push_back ({ m, false, true });
    }
    std::stable_sort (rows.begin(), rows.end(), [] (const Row& a, const Row& b) { return String (a.map.name).compareNatural (String (b.map.name)) < 0; });
    maps.updateContent();
    int sel = rows.empty() ? -1 : 0;
    for (int i = 0; i < (int) rows.size(); ++i) if (String (rows[(size_t) i].map.name) == keep) sel = i;
    if (sel >= 0) maps.selectRow (sel);
    chosenChanged();
    repaint();
}

void ArticulationManagerView::choose (int row) { maps.selectRow (row); }

const ArticulationManagerView::Row* ArticulationManagerView::chosenRow() const
{
    const int r = maps.getSelectedRow();
    return r >= 0 && r < (int) rows.size() ? &rows[(size_t) r] : nullptr;
}

void ArticulationManagerView::articulationsChosen()
{
    // The field shows the first chosen articulation's delay.
    const auto* r = chosenRow();
    const int first = arts.getSelectedRow();
    const bool any = r != nullptr && first >= 0 && first < (int) r->map.articulations.size();
    delayField.setEnabled (any);
    delayLabel.setEnabled (any);
    delayField.setText (any ? String (r->map.articulations[(size_t) first].delayMs, 0) : String(), dontSendNotification);
}

void ArticulationManagerView::applyDelay()
{
    const auto* r = chosenRow();
    if (! r || ! delayField.isEnabled()) return;
    std::set<int> which;
    for (int i = 0; i < (int) r->map.articulations.size(); ++i) if (arts.isRowSelected (i)) which.insert (i);
    if (which.empty()) return;
    const double ms = delayField.getText().trim().getDoubleValue();
    bool same = true;
    for (int i : which) same = same && r->map.articulations[(size_t) i].delayMs == ms;
    if (same) return;
    const auto name = r->map.name;
    c.setArticulationDelay (name, which, ms);
    refresh();
    for (int i : which) arts.selectRow (i, false, i == *which.begin());
}

void ArticulationManagerView::chosenChanged()
{
    arts.updateContent();
    arts.repaint();
    articulationsChosen();
    const auto* r = chosenRow();
    useButton.setEnabled (r != nullptr && c.focus() != nullptr && c.focus()->holdsClips());
    bool anyLibrary = false;
    for (int i = 0; i < (int) rows.size(); ++i) anyLibrary |= maps.isRowSelected (i) && rows[(size_t) i].inLibrary;
    removeButton.setEnabled (anyLibrary);
    repaint (infoArea);
}

void ArticulationManagerView::Maps::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) owner.rows.size()) return;
    const auto& r = owner.rows[(size_t) row];
    if (selected) { g.setColour (theme::rule); g.fillRect (0, 0, w, h); }
    g.setColour (theme::text);
    g.setFont (theme::font());
    g.drawText (String (r.map.name), 8, 0, w * 6 / 10 - 8, h, Justification::centredLeft, true);
    g.setColour (theme::textSecondary);
    g.setFont (theme::font (theme::smallPoints));
    const String where = r.inLibrary && r.inSong ? "Library, This Song" : r.inLibrary ? "Library" : "This Song";
    g.drawText (String ((int) r.map.articulations.size()) + ui::u8 (" \xc2\xb7 ") + where, w * 6 / 10, 0, w * 4 / 10 - 8, h, Justification::centredRight, true);
}

int ArticulationManagerView::Arts::getNumRows()
{
    auto* r = owner.chosenRow();
    return r ? (int) r->map.articulations.size() : 0;
}

void ArticulationManagerView::Arts::paintListBoxItem (int row, Graphics& g, int w, int h, bool selected)
{
    auto* r = owner.chosenRow();
    if (! r || row < 0 || row >= (int) r->map.articulations.size()) return;
    const auto& a = r->map.articulations[(size_t) row];
    if (selected) { g.setColour (theme::rule); g.fillRect (0, 0, w, h); }
    g.setColour (a.colour ? Colour (a.colour) : theme::control);
    g.fillRoundedRectangle (6.0f, (float) h * 0.25f, 6.0f, (float) h * 0.5f, 2.0f);
    g.setColour (theme::text);
    g.setFont (theme::font());
    g.drawText (String (a.name), 18, 0, w * 4 / 10 - 18, h, Justification::centredLeft, true);
    g.setColour (theme::textSecondary);
    g.setFont (theme::font (theme::smallPoints));
    String sends = articulations::describe (a);
    if (a.delayMs != 0) sends << ui::u8 (" \xc2\xb7 ") << String (a.delayMs, 0) << " ms";
    g.drawText (sends, w * 4 / 10, 0, w * 6 / 10 - 8, h, Justification::centredLeft, true);
}

void ArticulationManagerView::paint (Graphics& g)
{
    g.fillAll (theme::ground);
    String info;
    if (auto* r = chosenRow())
        info = String (r->map.name) + ": " + String ((int) r->map.articulations.size()) + " articulations, what each sends before its note. "
               "Choose articulations to set their delay.";
    else
        info = "No articulation maps yet. Import a Cubase expression map, a Logic articulation set or a Reaticulate bank.";
    if (auto* t = c.focus())
        info << " The chosen track is " << String (t->name).quoted() << ".";
    g.setColour (theme::textSecondary);
    g.setFont (theme::font (theme::smallPoints));
    g.drawFittedText (info, infoArea, Justification::centredLeft, 2);
}

void ArticulationManagerView::resized()
{
    auto r = getLocalBounds().reduced (theme::px (8), theme::px (6));
    auto bar = r.removeFromTop (theme::px (24));
    for (auto* b : { &importButton, &useButton, &removeButton })
    {
        b->setBounds (bar.removeFromLeft (b->idealWidth (theme::smallControl)));
        bar.removeFromLeft (theme::px (6));
    }
    r.removeFromTop (theme::px (4));
    infoArea = r.removeFromTop (theme::px (30));
    maps.setBounds (r.removeFromLeft (r.getWidth() * 2 / 5));
    r.removeFromLeft (theme::px (8));
    auto field = r.removeFromBottom (theme::px (26));
    r.removeFromBottom (theme::px (4));
    delayField.setBounds (field.removeFromRight (theme::px (70)));
    field.removeFromRight (theme::px (6));
    delayLabel.setBounds (field.removeFromRight (theme::px (90)));
    arts.setBounds (r);
}

//==============================================================================
ArticulationManagerWindow::ArticulationManagerWindow (Controller& c)
    : DocumentWindow ("Articulation Manager", theme::ground, DocumentWindow::closeButton)
{
    setUsingNativeTitleBar (true);
    setContentOwned (new ArticulationManagerView (c), false);
    setResizable (true, false);
    setResizeLimits (640, 320, 10000, 10000);
    setSize (900, 520);
}

bool ArticulationManagerWindow::keyPressed (const KeyPress& k)
{
    if (k == KeyPress ('w', ModifierKeys::commandModifier, 0) || k.getKeyCode() == KeyPress::escapeKey)
    {
        setVisible (false);
        return true;
    }
    return DocumentWindow::keyPressed (k);
}

} // namespace kal
