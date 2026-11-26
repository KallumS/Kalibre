#include "Browser.h"
#include "PluginPictures.h"

#include <array>

namespace kal
{
using namespace juce;

// The plug-ins under three headings (0125): MIDI Effects, Instruments, Audio
// Effects. A heading folds its plug-ins away when clicked (0138); a plug-in
// with a picture has it under its name, over the rows after it (`slice`).
struct Browser::PluginList : public ListBoxModel
{
    PluginList (Controller& ctrl) : c (ctrl) {}

    // A heading (its role), a plug-in, or a part of the picture under a plug-in.
    struct Line { int heading = -1; int type = -1; int count = 0; int slice = -1; int owner = -1; };
    std::vector<Line> lines;
    std::array<bool, 3> shut {};   // headings folded away
    bool searching = false;
    bool showPictures = true;   // the Pictures button: off, names only
    ListBox* box = nullptr;
    static constexpr int pictureRows = 4;   // a picture's height, in rows

    void filter (const String& text)
    {
        lines.clear();
        searching = text.isNotEmpty();
        const auto types = c.engine.plugins().known().getTypes();
        std::vector<int> byRole[3];
        for (int i = 0; i < types.size(); ++i)
        {
            const auto& d = types.getReference (i);
            if (text.isEmpty() || d.name.containsIgnoreCase (text) || d.manufacturerName.containsIgnoreCase (text)
                || d.pluginFormatName.containsIgnoreCase (text) || d.category.containsIgnoreCase (text))
                byRole[(int) c.engine.plugins().roleOf (d)].push_back (i);
        }
        for (int r = 0; r < 3; ++r)
        {
            if (byRole[r].empty()) continue;
            std::sort (byRole[r].begin(), byRole[r].end(), [&types] (int a, int b) { return types[a].name.compareIgnoreCase (types[b].name) < 0; });
            lines.push_back ({ r, -1, (int) byRole[r].size() });
            // Folded away: only the heading, unless searching (what's found is always shown).
            if (isShut (r)) continue;
            for (int i : byRole[r])
            {
                const int owner = (int) lines.size();
                lines.push_back ({ -1, i, 0 });
                if (showPictures && pictures::has (types.getReference (i)))
                    for (int k = 0; k < pictureRows; ++k) lines.push_back ({ -1, i, 0, k, owner });
            }
        }
    }

    bool isShut (int heading) const { return ! searching && shut[(size_t) heading]; }

    // The plug-in's own row, for a row of its picture.
    int ownerOf (int row) const
    {
        if (row < 0 || row >= (int) lines.size()) return row;
        return lines[(size_t) row].slice >= 0 ? lines[(size_t) row].owner : row;
    }

    const PluginDescription* at (int row) const
    {
        if (row < 0 || row >= (int) lines.size() || lines[(size_t) row].type < 0) return nullptr;
        types = c.engine.plugins().known().getTypes();
        return &types.getReference (lines[(size_t) row].type);
    }

    int getNumRows() override { return (int) lines.size(); }

    static String headingName (int heading)
    {
        const char* names[] { "MIDI Effects", "Instruments", "Audio Effects" };
        return names[heading];
    }

    void paintListBoxItem (int row, Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= (int) lines.size()) return;
        const auto& line = lines[(size_t) row];
        if (line.heading >= 0)
        {
            // A triangle that says whether it's open: pointing down, or right when folded.
            const bool open = ! isShut (line.heading);
            const float s = (float) theme::px (8), cx = 8.0f + s * 0.5f, cy = (float) h - (float) theme::px (9);
            Path tri;
            if (open) tri.addTriangle (cx - s * 0.5f, cy - s * 0.3f, cx + s * 0.5f, cy - s * 0.3f, cx, cy + s * 0.4f);
            else tri.addTriangle (cx - s * 0.3f, cy - s * 0.5f, cx - s * 0.3f, cy + s * 0.5f, cx + s * 0.4f, cy);
            g.setColour (searching ? theme::textSecondary : theme::text);
            g.fillPath (tri);
            const int x = 8 + theme::px (8) + theme::px (6);
            g.setColour (theme::text);
            g.setFont (theme::font (theme::bodyPoints, true));
            g.drawText (headingName (line.heading), x, 0, w - 60 - x, h, Justification::bottomLeft, true);
            g.setColour (theme::textSecondary);
            g.setFont (theme::font (theme::smallPoints));
            g.drawText (String (line.count), w - 60, 0, 52, h, Justification::bottomRight, false);
            g.setColour (theme::rule);
            g.fillRect (8, h - 1, w - 16, 1);
            return;
        }
        const auto* d = at (row);
        if (! d) return;
        if (selected || (line.slice >= 0 && box != nullptr && box->isRowSelected (line.owner))) { g.setColour (theme::rule); g.fillRect (0, 0, w, h); }
        if (line.slice >= 0)
        {
            // This row's part of the picture: the picture drawn whole, moved up by the rows above.
            const auto img = pictures::of (*d);
            if (img.isNull()) return;
            const Rectangle<float> room (8.0f, 2.0f, (float) (w - 16), (float) (pictureRows * h - 8));
            const auto shown = RectanglePlacement (RectanglePlacement::xLeft | RectanglePlacement::yTop | RectanglePlacement::onlyReduceInSize)
                                   .appliedTo (img.getBounds().toFloat(), room);
            Graphics::ScopedSaveState keep (g);
            g.setOrigin (0, -line.slice * h);
            g.setImageResamplingQuality (Graphics::highResamplingQuality);
            g.drawImage (img, shown);
            g.setColour (theme::rule);
            g.drawRect (shown.expanded (0.5f), 1.0f);
            return;
        }
        g.setColour (theme::text);
        g.setFont (theme::font());
        g.drawText (d->name, 8, 0, w - 70, h * 3 / 5, Justification::bottomLeft, true);
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        g.drawText (d->manufacturerName, 8, h * 3 / 5, w - 70, h * 2 / 5, Justification::topLeft, true);
        g.drawText (d->pluginFormatName, w - 60, 0, 52, h, Justification::centredRight, false);
    }

    // A row of a picture chosen is its plug-in chosen.
    void selectedRowsChanged (int last) override
    {
        if (box == nullptr) return;
        if (last >= 0 && last < (int) lines.size() && lines[(size_t) last].slice >= 0)
        {
            box->selectRow (lines[(size_t) last].owner, true, true);
            return;
        }
        box->repaint();
    }

    var getDragSourceDescription (const SparseSet<int>& rows) override
    {
        if (rows.isEmpty() || ! at (rows[0])) return {};
        return "plugin:" + String (lines[(size_t) rows[0]].type);
    }

    void addToChosen (const PluginDescription& d)
    {
        Id track = c.focusTrack;
        if (track == 0) { c.addPluginOnNewTrack (d); return; }
        c.addPlugin (track, d);
    }

    void listBoxItemDoubleClicked (int row, const MouseEvent&) override
    {
        if (const auto* d = at (row)) addToChosen (*d);
    }

    // A heading clicked opens or folds it; a plug-in right-clicked: a new track
    // with it, the chosen track, or where its file is.
    void listBoxItemClicked (int row, const MouseEvent& e) override
    {
        if (row >= 0 && row < (int) lines.size() && lines[(size_t) row].heading >= 0)
        {
            if (! e.mods.isPopupMenu()) toggle (lines[(size_t) row].heading);
            return;
        }
        const auto* found = at (row);
        if (! found || ! e.mods.isPopupMenu()) return;
        const auto d = *found;
        PopupMenu m;
        m.addItem ("Add to a New Track", [this, d] { c.addPluginOnNewTrack (d); });
        m.addItem ("Add to the Chosen Track", c.project.track (c.focusTrack) != nullptr, false, [this, d] { addToChosen (d); });
        m.addSeparator();
        const auto file = Controller::pluginFile (d);
        m.addItem (ui::words().showInFiles, file.exists(), false, [file] { file.revealToUser(); });
        const auto picture = pictures::fileFor (d);
        m.addItem (String (ui::words().showInFiles).replace ("Show in", "Show Picture in"), picture.existsAsFile(), false, [picture] { picture.revealToUser(); });
        m.showMenuAsync (PopupMenu::Options());
    }

    std::function<void()> onToggled;
    void toggle (int heading)
    {
        if (searching) return;   // while searching, everything found is shown
        shut[(size_t) heading] = ! shut[(size_t) heading];
        if (onToggled) onToggled();
    }

    String getNameForRow (int row) override
    {
        if (row >= 0 && row < (int) lines.size() && lines[(size_t) row].heading >= 0)
            return headingName (lines[(size_t) row].heading) + (isShut (lines[(size_t) row].heading) ? ", folded" : "");
        if (row >= 0 && row < (int) lines.size() && lines[(size_t) row].slice >= 0)
        {
            const auto* d = at (row);
            return d ? "Picture of " + d->name : String();
        }
        const auto* d = at (row);
        return d ? d->name : String();
    }

    Controller& c;
    mutable Array<PluginDescription> types;
};

struct Browser::PresetList : public ListBoxModel
{
    PresetList (Controller& ctrl) : c (ctrl) {}

    struct Entry { Id device; String plugin; File file; };
    std::vector<Entry> entries;

    void refresh (const String& filter)
    {
        entries.clear();
        const auto* t = c.project.track (c.focusTrack);
        if (! t) return;
        for (auto& d : t->devices)
            for (auto& f : c.presetsFor (d.plugin))
                if (filter.isEmpty() || f.getFileNameWithoutExtension().containsIgnoreCase (filter) || String (d.plugin.name).containsIgnoreCase (filter))
                    entries.push_back ({ d.id, String (d.plugin.name), f });
    }

    int getNumRows() override { return (int) entries.size(); }

    void paintListBoxItem (int row, Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= (int) entries.size()) return;
        const auto& e = entries[(size_t) row];
        if (selected) { g.setColour (theme::rule); g.fillRect (0, 0, w, h); }
        g.setColour (theme::text);
        g.setFont (theme::font());
        g.drawText (e.file.getFileNameWithoutExtension(), 8, 0, w - 16, h * 3 / 5, Justification::bottomLeft, true);
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        g.drawText (e.plugin, 8, h * 3 / 5, w - 16, h * 2 / 5, Justification::topLeft, true);
    }

    void listBoxItemDoubleClicked (int row, const MouseEvent&) override
    {
        if (row >= 0 && row < (int) entries.size()) c.loadPreset (entries[(size_t) row].device, entries[(size_t) row].file);
    }

    // Right-click, as the Sounds tab (0125): load it, on a new track, show it, delete it.
    void listBoxItemClicked (int row, const MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() || row < 0 || row >= (int) entries.size()) return;
        const auto en = entries[(size_t) row];
        PopupMenu m;
        m.addItem ("Load", [this, en] { c.loadPreset (en.device, en.file); });
        m.addItem ("Load on a New Track", [this, en] { c.presetOnNewTrack (en.device, en.file); });
        m.addSeparator();
        m.addItem (ui::words().showInFiles, [en] { en.file.revealToUser(); });
        m.addItem (ui::u8 ("Delete\xe2\x80\xa6"), [this, en]
        {
            auto* w = new AlertWindow ("Delete the preset " + en.file.getFileNameWithoutExtension().quoted() + "?",
                                       "It goes to " + String (ui::words().trash) + ".", MessageBoxIconType::NoIcon);
            w->addButton ("Delete", 1, KeyPress (KeyPress::returnKey));
            w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
            ui::present (w, ModalCallbackFunction::create ([this, en] (int r)
            {
                if (r != 1) return;
                en.file.moveToTrash();
                c.say ("Deleted the preset " + en.file.getFileNameWithoutExtension().quoted() + ".");
                c.viewChanged();
            }), true);
        });
        m.showMenuAsync (PopupMenu::Options());
    }

    Controller& c;
};

//==============================================================================
Browser::Browser (Controller& ctrl) : c (ctrl)
{
    setTitle ("Browser");
    for (auto* b : { &pluginsTab, &presetsTab, &filesTab, &audioTab })
    {
        b->setRadioGroupId (3);
        b->setClickingTogglesState (true);
        addAndMakeVisible (b);
    }
    pluginsTab.onClick = [this] { showTab (Tab::Plugins); };
    filesTab.onClick = [this] { showTab (Tab::Files); };
    audioTab.onClick = [this] { showTab (Tab::Audio); };
    addChildComponent (cores);
    cores.onClick = [this]
    {
        c.engine.setParallel (cores.getToggleState());
        c.say (cores.getToggleState() ? "The tracks play on " + String (c.engine.audioThreads()) + " cores at once."
                                      : String ("Everything plays on one core, one track after another."));
    };
    presetsTab.onClick = [this] { showTab (Tab::Presets); };
    addAndMakeVisible (search);
    search.setTextToShowWhenEmpty ("Search", theme::textSecondary);
    search.setTitle ("Search");
    search.onTextChange = [this]
    {
        plugins->filter (search.getText());
        pluginBox.updateContent();
        sounds->setFilter (search.getText());
        presets->refresh (search.getText());
        presetBox.updateContent();
        repaint();
    };
    addAndMakeVisible (scan);
    scan.onClick = [this] { c.scanPlugins(); };
    addAndMakeVisible (picturesButton);
    picturesButton.setToggleState (true, dontSendNotification);
    picturesButton.onClick = [this] { showPictures (picturesButton.getToggleState()); };

    plugins = std::make_unique<PluginList> (c);
    plugins->box = &pluginBox;
    plugins->onToggled = [this] { refreshPlugins(); };
    plugins->filter ({});
    pluginBox.setModel (plugins.get());
    pluginBox.setRowHeight (theme::px (34));
    pluginBox.setTitle ("Plug-ins");
    addAndMakeVisible (pluginBox);
    sounds = std::make_unique<LibraryView> (c);
    addChildComponent (*sounds);
    presets = std::make_unique<PresetList> (c);
    presetBox.setModel (presets.get());
    presetBox.setRowHeight (theme::px (34));
    presetBox.setTitle ("Presets");
    addChildComponent (presetBox);
    c.addChangeListener (this);

    c.engine.plugins().known().addChangeListener (this);
    pictures::changes().addChangeListener (this);
    showTab (Tab::Plugins);
}

Browser::~Browser()
{
    c.removeChangeListener (this);
    c.engine.plugins().known().removeChangeListener (this);
    pictures::changes().removeChangeListener (this);
}

void Browser::refreshPlugins()
{
    plugins->filter (search.getText());
    pluginBox.updateContent();
    pluginBox.repaint();
}

std::array<bool, 3> Browser::shutHeadings() const { return plugins->shut; }
void Browser::setShutHeadings (std::array<bool, 3> shut) { plugins->shut = shut; refreshPlugins(); }
void Browser::toggleHeading (int role) { plugins->toggle (role); }
bool Browser::picturesShown() const { return plugins->showPictures; }
void Browser::showPictures (bool on)
{
    plugins->showPictures = on;
    picturesButton.setToggleState (on, dontSendNotification);
    refreshPlugins();
}

void Browser::changeListenerCallback (ChangeBroadcaster* from)
{
    if (from == &c)
    {
        if (tab == Tab::Presets) { presets->refresh (search.getText()); presetBox.updateContent(); presetBox.repaint(); }
        return;
    }
    refreshPlugins();
}

Browser::Tab Browser::tabFromNumber (int n)
{
    switch (n)
    {
        case 1: return Tab::Files;
        case 2: return Tab::Audio;
        case 4: return Tab::Presets;
        default: return Tab::Plugins;   // 3 and 5 were Studio and Keys, windows of their own now
    }
}

void Browser::showTab (Tab t)
{
    tab = t;
    pluginsTab.setToggleState (t == Tab::Plugins, dontSendNotification);
    filesTab.setToggleState (t == Tab::Files, dontSendNotification);
    audioTab.setToggleState (t == Tab::Audio, dontSendNotification);
    presetsTab.setToggleState (t == Tab::Presets, dontSendNotification);
    presetBox.setVisible (t == Tab::Presets);
    if (t == Tab::Presets) { presets->refresh (search.getText()); presetBox.updateContent(); }
    pluginBox.setVisible (t == Tab::Plugins);
    scan.setVisible (t == Tab::Plugins);
    picturesButton.setVisible (t == Tab::Plugins);
    sounds->setVisible (t == Tab::Files);
    search.setVisible (t == Tab::Plugins || t == Tab::Files || t == Tab::Presets);
    cores.setVisible (t == Tab::Audio);
    cores.setToggleState (c.engine.isParallel(), dontSendNotification);
    cores.setButtonText ("Use Every Core (" + String (ParallelGraph::coresToUse()) + ")");
    if (t == Tab::Audio && ! audio)
    {
        audio = std::make_unique<AudioDeviceSelectorComponent> (c.engine.devices(), 0, 16, 2, 16, true, true, true, false);
        addAndMakeVisible (*audio);
    }
    if (audio) audio->setVisible (t == Tab::Audio);
    resized();
    repaint();
}

void Browser::paint (Graphics& g)
{
    g.fillAll (theme::ground);
    g.setColour (theme::rule);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
}

void Browser::resized()
{
    auto r = getLocalBounds().reduced (theme::px (8));
    // Two rows of two.
    auto placeRow = [&] (std::initializer_list<theme::HouseButton*> row)
    {
        auto tabs = r.removeFromTop (theme::px (24));
        const int w = (tabs.getWidth() - 4) / 2;
        for (auto* b : row) { b->setBounds (tabs.removeFromLeft (w)); tabs.removeFromLeft (4); }
        r.removeFromTop (4);
    };
    placeRow ({ &pluginsTab, &presetsTab });
    placeRow ({ &filesTab, &audioTab });
    r.removeFromTop (theme::px (8));
    if (tab != Tab::Audio)
    {
        auto row = r.removeFromTop (theme::px (26));
        if (tab == Tab::Plugins)
        {
            scan.setBounds (row.removeFromRight (scan.idealWidth (theme::smallControl)));
            row.removeFromRight (4);
            picturesButton.setBounds (row.removeFromRight (picturesButton.idealWidth (theme::smallControl)));
            row.removeFromRight (4);
        }
        search.setBounds (row);
        r.removeFromTop (theme::px (6));
    }
    pluginBox.setBounds (r);
    sounds->setBounds (r);
    presetBox.setBounds (r);
    if (tab == Tab::Audio)
    {
        cores.setBounds (r.removeFromTop (theme::px (26)).withWidth (cores.idealWidth (theme::smallControl)));
        r.removeFromTop (theme::px (6));
    }
    if (audio) audio->setBounds (r);
}

} // namespace kal
