#include "Browser.h"

namespace kal
{
using namespace juce;

struct Browser::PluginList : public ListBoxModel
{
    PluginList (Controller& ctrl) : c (ctrl) {}

    void filter (const String& text)
    {
        shown.clear();
        const auto types = c.engine.plugins().known().getTypes();
        for (int i = 0; i < types.size(); ++i)
        {
            const auto& d = types.getReference (i);
            if (text.isEmpty() || d.name.containsIgnoreCase (text) || d.manufacturerName.containsIgnoreCase (text)
                || d.pluginFormatName.containsIgnoreCase (text) || d.category.containsIgnoreCase (text))
                shown.push_back (i);
        }
        std::sort (shown.begin(), shown.end(), [&types] (int a, int b) { return types[a].name.compareIgnoreCase (types[b].name) < 0; });
    }

    int getNumRows() override { return (int) shown.size(); }

    void paintListBoxItem (int row, Graphics& g, int w, int h, bool selected) override
    {
        const auto types = c.engine.plugins().known().getTypes();
        if (row < 0 || row >= (int) shown.size()) return;
        const auto& d = types.getReference (shown[(size_t) row]);
        if (selected) { g.setColour (theme::rule); g.fillRect (0, 0, w, h); }
        g.setColour (theme::text);
        g.setFont (theme::font());
        g.drawText (d.name, 8, 0, w - 70, h * 3 / 5, Justification::bottomLeft, true);
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        const char* role[] { "MIDI effect", "Instrument", "Effect" };
        g.drawText (d.manufacturerName + " " + ui::u8 ("\xc2\xb7") + " " + role[(int) c.engine.plugins().roleOf (d)], 8, h * 3 / 5, w - 70, h * 2 / 5, Justification::topLeft, true);
        g.drawText (d.pluginFormatName, w - 60, 0, 52, h, Justification::centredRight, false);
    }

    var getDragSourceDescription (const SparseSet<int>& rows) override
    {
        if (rows.isEmpty()) return {};
        return "plugin:" + String (shown[(size_t) rows[0]]);
    }

    void listBoxItemDoubleClicked (int row, const MouseEvent&) override
    {
        const auto types = c.engine.plugins().known().getTypes();
        if (row < 0 || row >= (int) shown.size()) return;
        Id track = c.focusTrack;
        const auto& d = types.getReference (shown[(size_t) row]);
        if (track == 0) track = c.addTrack (TrackKind::Track, d.name);
        c.addPlugin (track, d);
    }

    String getNameForRow (int row) override
    {
        const auto types = c.engine.plugins().known().getTypes();
        return row >= 0 && row < (int) shown.size() ? types[shown[(size_t) row]].name : String();
    }

    Controller& c;
    std::vector<int> shown;
};

struct Browser::FileList : public ListBoxModel
{
    FileList (Browser& b) : owner (b)
    {
        folder = File::getSpecialLocation (File::userMusicDirectory);
        if (! folder.isDirectory()) folder = File::getSpecialLocation (File::userHomeDirectory);
        refresh ({});
    }

    void refresh (const String& filter)
    {
        entries.clear();
        for (const auto& e : RangedDirectoryIterator (folder, false, "*", File::findFilesAndDirectories))
        {
            const auto f = e.getFile();
            if (f.isHidden()) continue;
            if (! f.isDirectory() && ! f.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;mid;midi")) continue;
            if (filter.isNotEmpty() && ! f.getFileName().containsIgnoreCase (filter)) continue;
            entries.push_back (f);
        }
        std::sort (entries.begin(), entries.end(), [] (const File& a, const File& b)
        {
            if (a.isDirectory() != b.isDirectory()) return a.isDirectory();
            return a.getFileName().compareIgnoreCase (b.getFileName()) < 0;
        });
    }

    int getNumRows() override { return (int) entries.size(); }

    void paintListBoxItem (int row, Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= (int) entries.size()) return;
        const auto& f = entries[(size_t) row];
        if (selected) { g.setColour (theme::rule); g.fillRect (0, 0, w, h); }
        g.setColour (f.isDirectory() ? theme::textSecondary : theme::text);
        g.setFont (theme::font());
        g.drawText ((f.isDirectory() ? ui::u8 ("\xe2\x96\xb8 ") : String()) + f.getFileName(), 8, 0, w - 16, h, Justification::centredLeft, true);
    }

    void listBoxItemClicked (int row, const MouseEvent&) override
    {
        if (row >= 0 && row < (int) entries.size() && ! entries[(size_t) row].isDirectory()) owner.preview (entries[(size_t) row]);
    }

    void listBoxItemDoubleClicked (int row, const MouseEvent&) override
    {
        if (row < 0 || row >= (int) entries.size()) return;
        const auto f = entries[(size_t) row];
        if (f.isDirectory()) { folder = f; refresh ({}); owner.fileBox.updateContent(); owner.repaint(); return; }
        owner.c.importFile (f, owner.c.focusTrack, owner.c.cursor);
    }

    var getDragSourceDescription (const SparseSet<int>& rows) override
    {
        if (rows.isEmpty() || entries[(size_t) rows[0]].isDirectory()) return {};
        return "file:" + entries[(size_t) rows[0]].getFullPathName();
    }

    Browser& owner;
    File folder;
    std::vector<File> entries;
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

    Controller& c;
};

//==============================================================================
Browser::Browser (Controller& ctrl) : c (ctrl), studio (ctrl), keysPanel (ctrl.keys)
{
    setTitle ("Browser");
    addChildComponent (studioView);
    studioView.setViewedComponent (&studio, false);
    studioView.setScrollBarsShown (true, false);
    for (auto* b : { &pluginsTab, &presetsTab, &filesTab, &audioTab, &studioTab, &keysTab })
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
    studioTab.onClick = [this] { showTab (Tab::Studio); };
    presetsTab.onClick = [this] { showTab (Tab::Presets); };
    keysTab.onClick = [this] { showTab (Tab::Keys); };
    addChildComponent (keysPanel);
    addAndMakeVisible (search);
    search.setTextToShowWhenEmpty ("Search", theme::textSecondary);
    search.setTitle ("Search");
    search.onTextChange = [this]
    {
        plugins->filter (search.getText());
        pluginBox.updateContent();
        files->refresh (search.getText());
        fileBox.updateContent();
        presets->refresh (search.getText());
        presetBox.updateContent();
        repaint();
    };
    addAndMakeVisible (scan);
    scan.onClick = [this] { c.scanPlugins(); };
    addChildComponent (up);
    up.onClick = [this]
    {
        files->folder = files->folder.getParentDirectory();
        files->refresh (search.getText());
        fileBox.updateContent();
        repaint();
    };

    plugins = std::make_unique<PluginList> (c);
    plugins->filter ({});
    pluginBox.setModel (plugins.get());
    pluginBox.setRowHeight (theme::px (34));
    pluginBox.setTitle ("Plug-ins");
    addAndMakeVisible (pluginBox);
    files = std::make_unique<FileList> (*this);
    fileBox.setModel (files.get());
    fileBox.setRowHeight (theme::px (24));
    fileBox.setTitle ("Files");
    addChildComponent (fileBox);
    presets = std::make_unique<PresetList> (c);
    presetBox.setModel (presets.get());
    presetBox.setRowHeight (theme::px (34));
    presetBox.setTitle ("Presets");
    addChildComponent (presetBox);
    c.addChangeListener (this);
    for (auto* box : { &pluginBox, &fileBox, &presetBox })
    {
        box->setColour (ListBox::backgroundColourId, theme::sunken);
        box->setColour (ListBox::outlineColourId, theme::rule);
    }

    formats.registerBasicFormats();
    previewPlayer.setSource (&transport);
    c.engine.devices().addAudioCallback (&previewPlayer);
    c.engine.plugins().known().addChangeListener (this);
    showTab (Tab::Plugins);
}

Browser::~Browser()
{
    c.removeChangeListener (this);
    c.engine.plugins().known().removeChangeListener (this);
    c.engine.devices().removeAudioCallback (&previewPlayer);
    transport.setSource (nullptr);
    previewPlayer.setSource (nullptr);
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

void Browser::preview (const File& f)
{
    transport.stop();
    transport.setSource (nullptr);
    reader.reset();
    if (f.hasFileExtension ("mid;midi")) return;
    if (auto* r = formats.createReaderFor (f))
    {
        reader = std::make_unique<AudioFormatReaderSource> (r, true);
        transport.setSource (reader.get(), 0, nullptr, r->sampleRate);
        transport.setGain (0.7f);
        transport.start();
    }
}

void Browser::showTab (Tab t)
{
    tab = t;
    pluginsTab.setToggleState (t == Tab::Plugins, dontSendNotification);
    filesTab.setToggleState (t == Tab::Files, dontSendNotification);
    audioTab.setToggleState (t == Tab::Audio, dontSendNotification);
    studioTab.setToggleState (t == Tab::Studio, dontSendNotification);
    presetsTab.setToggleState (t == Tab::Presets, dontSendNotification);
    keysTab.setToggleState (t == Tab::Keys, dontSendNotification);
    keysPanel.setVisible (t == Tab::Keys);
    if (t == Tab::Keys) keysPanel.refresh();
    presetBox.setVisible (t == Tab::Presets);
    if (t == Tab::Presets) { presets->refresh (search.getText()); presetBox.updateContent(); }
    studioView.setVisible (t == Tab::Studio);
    pluginBox.setVisible (t == Tab::Plugins);
    scan.setVisible (t == Tab::Plugins);
    fileBox.setVisible (t == Tab::Files);
    up.setVisible (t == Tab::Files);
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
    if (tab == Tab::Files)
    {
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        g.drawFittedText (files->folder.getFullPathName(), up.getBounds().withLeft (up.getRight() + 6).withRight (getWidth() - 8), Justification::centredLeft, 1);
    }
    g.setColour (theme::rule);
    g.fillRect (getWidth() - 1, 0, 1, getHeight());
}

void Browser::resized()
{
    auto r = getLocalBounds().reduced (theme::px (8));
    // Two rows of three.
    auto placeRow = [&] (std::initializer_list<theme::HouseButton*> row)
    {
        auto tabs = r.removeFromTop (theme::px (24));
        const int w = (tabs.getWidth() - 8) / 3;
        for (auto* b : row) { b->setBounds (tabs.removeFromLeft (w)); tabs.removeFromLeft (4); }
        r.removeFromTop (4);
    };
    placeRow ({ &pluginsTab, &presetsTab, &filesTab });
    placeRow ({ &audioTab, &studioTab, &keysTab });
    r.removeFromTop (theme::px (8));
    if (tab == Tab::Keys) { keysPanel.setBounds (r); return; }
    if (tab == Tab::Studio)
    {
        studioView.setBounds (r);
        studio.setSize (r.getWidth() - studioView.getScrollBarThickness(), jmax (r.getHeight(), studio.preferredHeight()));
        return;
    }
    if (tab != Tab::Audio)
    {
        auto row = r.removeFromTop (theme::px (26));
        if (tab == Tab::Plugins) { scan.setBounds (row.removeFromRight (scan.idealWidth (theme::smallControl))); row.removeFromRight (4); }
        search.setBounds (row);
        r.removeFromTop (theme::px (6));
    }
    if (tab == Tab::Files)
    {
        auto row = r.removeFromTop (theme::px (24));
        up.setBounds (row.removeFromLeft (theme::px (28)));
        r.removeFromTop (theme::px (4));
    }
    pluginBox.setBounds (r);
    fileBox.setBounds (r);
    presetBox.setBounds (r);
    if (tab == Tab::Audio)
    {
        cores.setBounds (r.removeFromTop (theme::px (26)).withWidth (cores.idealWidth (theme::smallControl)));
        r.removeFromTop (theme::px (6));
    }
    if (audio) audio->setBounds (r);
}

} // namespace kal
