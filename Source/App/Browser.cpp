#include "Browser.h"

namespace kal
{
using namespace juce;

// The plug-ins under three headings (0125): MIDI Effects, Instruments, Audio Effects.
struct Browser::PluginList : public ListBoxModel
{
    PluginList (Controller& ctrl) : c (ctrl) {}

    struct Line { int heading = -1; int type = -1; int count = 0; };   // a heading (its role) or a plug-in
    std::vector<Line> lines;

    void filter (const String& text)
    {
        lines.clear();
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
            for (int i : byRole[r]) lines.push_back ({ -1, i, 0 });
        }
    }

    const PluginDescription* at (int row) const
    {
        if (row < 0 || row >= (int) lines.size() || lines[(size_t) row].type < 0) return nullptr;
        types = c.engine.plugins().known().getTypes();
        return &types.getReference (lines[(size_t) row].type);
    }

    int getNumRows() override { return (int) lines.size(); }

    void paintListBoxItem (int row, Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= (int) lines.size()) return;
        const auto& line = lines[(size_t) row];
        if (line.heading >= 0)
        {
            const char* names[] { "MIDI Effects", "Instruments", "Audio Effects" };
            g.setColour (theme::text);
            g.setFont (theme::font (theme::bodyPoints, true));
            g.drawText (names[line.heading], 8, 0, w - 60, h, Justification::bottomLeft, true);
            g.setColour (theme::textSecondary);
            g.setFont (theme::font (theme::smallPoints));
            g.drawText (String (line.count), w - 60, 0, 52, h, Justification::bottomRight, false);
            g.setColour (theme::rule);
            g.fillRect (8, h - 1, w - 16, 1);
            return;
        }
        const auto* d = at (row);
        if (! d) return;
        if (selected) { g.setColour (theme::rule); g.fillRect (0, 0, w, h); }
        g.setColour (theme::text);
        g.setFont (theme::font());
        g.drawText (d->name, 8, 0, w - 70, h * 3 / 5, Justification::bottomLeft, true);
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        g.drawText (d->manufacturerName, 8, h * 3 / 5, w - 70, h * 2 / 5, Justification::topLeft, true);
        g.drawText (d->pluginFormatName, w - 60, 0, 52, h, Justification::centredRight, false);
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

    // Right-click: a new track with it, the chosen track, or where its file is.
    void listBoxItemClicked (int row, const MouseEvent& e) override
    {
        const auto* found = at (row);
        if (! found || ! e.mods.isPopupMenu()) return;
        const auto d = *found;
        PopupMenu m;
        m.addItem ("Add to a New Track", [this, d] { c.addPluginOnNewTrack (d); });
        m.addItem ("Add to the Chosen Track", c.project.track (c.focusTrack) != nullptr, false, [this, d] { addToChosen (d); });
        m.addSeparator();
        const auto file = Controller::pluginFile (d);
        m.addItem ("Show in Finder", file.exists(), false, [file] { file.revealToUser(); });
        m.showMenuAsync (PopupMenu::Options());
    }

    String getNameForRow (int row) override
    {
        if (row >= 0 && row < (int) lines.size() && lines[(size_t) row].heading >= 0)
        {
            const char* names[] { "MIDI Effects", "Instruments", "Audio Effects" };
            return names[lines[(size_t) row].heading];
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
        m.addItem ("Show in Finder", [en] { en.file.revealToUser(); });
        m.addItem (ui::u8 ("Delete\xe2\x80\xa6"), [this, en]
        {
            auto* w = new AlertWindow ("Delete the preset " + en.file.getFileNameWithoutExtension().quoted() + "?",
                                       "It goes to the Trash.", MessageBoxIconType::NoIcon);
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

    plugins = std::make_unique<PluginList> (c);
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
    for (auto* box : { &pluginBox, &presetBox })
    {
        box->setColour (ListBox::backgroundColourId, theme::sunken);
        box->setColour (ListBox::outlineColourId, theme::rule);
    }

    c.engine.plugins().known().addChangeListener (this);
    showTab (Tab::Plugins);
}

Browser::~Browser()
{
    c.removeChangeListener (this);
    c.engine.plugins().known().removeChangeListener (this);
}

void Browser::changeListenerCallback (ChangeBroadcaster* from)
{
    if (from == &c)
    {
        if (tab == Tab::Presets) { presets->refresh (search.getText()); presetBox.updateContent(); presetBox.repaint(); }
        return;
    }
    plugins->filter (search.getText());
    pluginBox.updateContent();
    pluginBox.repaint();
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
        if (tab == Tab::Plugins) { scan.setBounds (row.removeFromRight (scan.idealWidth (theme::smallControl))); row.removeFromRight (4); }
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
