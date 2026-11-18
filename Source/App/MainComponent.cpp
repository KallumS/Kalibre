#include "MainComponent.h"

namespace kal
{
using namespace juce;

static ApplicationProperties& settingsStore()
{
    static ApplicationProperties p;
    static bool made = false;
    if (! made)
    {
        PropertiesFile::Options o;
        o.applicationName = "Kalibre";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        o.folderName = "Kalibre";
        p.setStorageParameters (o);
        made = true;
    }
    return p;
}

MainComponent::MainComponent (Controller& ctrl) : c (ctrl), top (ctrl), arrange (ctrl), lower (ctrl), browser (ctrl), surfaces (ctrl)
{
    ui::setHost (this);
    addAndMakeVisible (top);
    addAndMakeVisible (arrange);
    addAndMakeVisible (lower);
    addChildComponent (browser);
   #if ! JUCE_MAC
    menuBar = std::make_unique<MenuBarComponent> (this);
    addAndMakeVisible (*menuBar);
   #endif
    if (auto* s = settingsStore().getUserSettings())
    {
        lowerHeight = s->getIntValue ("lowerHeight", 320);
        browserShown = s->getBoolValue ("browser", false);
        c.zeroCrossings = s->getBoolValue ("zeroCrossings", false);
        c.hiddenInTopBar.addTokens (s->getValue ("hiddenInTopBar"), "|", {});
        c.hiddenInTopBar.removeEmptyStrings();
        c.cpuEverySeconds = jlimit (1, 5, s->getIntValue ("cpuEverySeconds", 2));
        c.arrangeGrid = jmax<Tick> (PPQ / 32, (Tick) s->getIntValue ("arrangeGrid", PPQ / 4));
        c.followPages = s->getBoolValue ("followPages", false);
        setTextSize ((float) s->getDoubleValue ("textSize", 1.0));
    }
    browser.setVisible (browserShown);
    setWantsKeyboardFocus (true);
    setTitle ("Kalibre");
    registerKeys();
    c.onShowPluginManager = [this] { showPluginManager(); };
    c.addChangeListener (this);
    setSize (1440, 900);
}

MainComponent::~MainComponent()
{
    c.keys.all().clear();   // their actions point at this window
    c.onShowPluginManager = nullptr;
    pluginWindow.reset();
    c.removeChangeListener (this);
    if (auto* s = settingsStore().getUserSettings())
    {
        s->setValue ("lowerHeight", lowerHeight);
        s->setValue ("browser", browserShown);
        s->setValue ("zeroCrossings", c.zeroCrossings);
        s->setValue ("hiddenInTopBar", c.hiddenInTopBar.joinIntoString ("|"));
        s->setValue ("cpuEverySeconds", c.cpuEverySeconds);
        s->setValue ("arrangeGrid", (int) c.arrangeGrid);
        s->setValue ("followPages", c.followPages);
        s->setValue ("textSize", (double) theme::scale());
        settingsStore().saveIfNeeded();
        settingsStore().closeFiles();
    }
    ui::setHost (nullptr);
}

void MainComponent::changeListenerCallback (ChangeBroadcaster*)
{
    if (auto* w = findParentComponentOfClass<DocumentWindow>())
        w->setName ((c.file.existsAsFile() ? c.file.getFileNameWithoutExtension() : String ("Untitled")) + (c.dirty ? ui::u8 (" \xe2\x80\x94 Edited") : String()));
    resized();
}

void MainComponent::showBrowser (Browser::Tab tab)
{
    browserShown = true;
    browser.setVisible (true);
    browser.showTab (tab);
    resized();
}

// Settings may have a window of their own (decision 0002, amended); the
// plug-ins are managed in one (decision 0047).
void MainComponent::showPluginManager()
{
    if (! pluginWindow)
    {
        pluginWindow = std::make_unique<PluginManagerWindow> (c);
        if (auto* top = getTopLevelComponent()) pluginWindow->centreAroundComponent (top, pluginWindow->getWidth(), pluginWindow->getHeight());
    }
    if (auto* view = dynamic_cast<PluginManagerView*> (pluginWindow->getContentComponent())) view->refresh();
    pluginWindow->setVisible (true);
    pluginWindow->toFront (true);
}

void MainComponent::setTextSize (float s)
{
    theme::setScale (s);
    sendLookAndFeelChange();
    resized();
    repaint();
}

Rectangle<int> MainComponent::dividerArea() const
{
    return { lower.getX(), lower.getY() - theme::px (6), lower.getWidth(), theme::px (6) };
}

void MainComponent::paint (Graphics& g)
{
    g.fillAll (theme::ground);
    const auto d = dividerArea();
    g.setColour (draggingDivider ? theme::control : theme::rule);
    g.fillRect (d.withSizeKeepingCentre (d.getWidth(), 2));
    g.setColour (draggingDivider ? theme::control : theme::edge);
    for (int i = -1; i <= 1; ++i)
        g.fillEllipse (Rectangle<float> (3.0f, 3.0f).withCentre (d.getCentre().toFloat().translated ((float) i * 6.0f, 0.0f)));
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    if (menuBar) menuBar->setBounds (r.removeFromTop (theme::px (24)));
    top.setBounds (r.getX(), r.getY(), r.getWidth(), 1);
    top.setBounds (r.removeFromTop (top.preferredHeight()));
    if (browserShown) browser.setBounds (r.removeFromLeft (theme::px (260)));
    // The zone keeps the height it was given; only what's shown is limited by the window.
    const int zone = jlimit (theme::px (120), jmax (theme::px (120), r.getHeight() - theme::px (160)), lowerHeight);
    lower.setBounds (r.removeFromBottom (zone));
    r.removeFromBottom (theme::px (6));
    arrange.setBounds (r);
}

void MainComponent::mouseMove (const MouseEvent& e)
{
    setMouseCursor (dividerArea().expanded (0, 2).contains (e.getPosition()) ? MouseCursor::UpDownResizeCursor : MouseCursor::NormalCursor);
}

void MainComponent::mouseDown (const MouseEvent& e)
{
    draggingDivider = dividerArea().expanded (0, 2).contains (e.getPosition());
    dragStartHeight = lowerHeight;
}

void MainComponent::mouseDrag (const MouseEvent& e)
{
    if (! draggingDivider) return;
    // Every plug-in window in the zone follows its height.
    lowerHeight = jlimit (theme::px (120), jmax (theme::px (120), getHeight() - theme::px (300)), dragStartHeight - e.getDistanceFromDragStartY());
    resized();
    repaint();
}

//==============================================================================
bool MainComponent::keyPressed (const KeyPress& k)
{
    // Every key is the user's to change (decision 0045), in the browser's Keys tab.
    return c.keys.handle (KeyPress (k.getKeyCode(), k.getModifiers().withoutMouseButtons(), 0));
}

File MainComponent::workspacesFolder() const
{
    auto f = c.scriptsFolder().getParentDirectory().getChildFile ("Workspaces");
    f.createDirectory();
    return f;
}

bool MainComponent::saveWorkspace (const String& name)
{
    DynamicObject::Ptr o = new DynamicObject();
    o->setProperty ("browser", browserShown);
    o->setProperty ("browserTab", (int) browser.shownTab());
    o->setProperty ("lowerHeight", lowerHeight);
    o->setProperty ("zoneTab", (int) c.zoneTab);
    o->setProperty ("editor", (int) c.editor);
    o->setProperty ("textSize", (double) theme::scale());
    o->setProperty ("chordLane", c.showChordLane);
    o->setProperty ("tempoLane", c.showTempoLane);
    o->setProperty ("sectionLane", c.showSectionLane);
    o->setProperty ("follow", c.followPlayback);
    o->setProperty ("zoom", arrange.timeline.pixelsPerTick);
    const auto f = workspacesFolder().getChildFile (File::createLegalFileName (name) + ".json");
    if (! f.replaceWithText (JSON::toString (var (o.get())))) { c.say ("The workspace couldn't be saved.", true); return false; }
    c.say ("Saved the workspace " + name.quoted() + ": View > Workspaces brings it back.");
    return true;
}

bool MainComponent::loadWorkspace (const File& f)
{
    const auto v = JSON::parse (f.loadFileAsString());
    if (! v.isObject()) return false;
    browserShown = (bool) v.getProperty ("browser", browserShown);
    browser.setVisible (browserShown);
    browser.showTab ((Browser::Tab) (int) v.getProperty ("browserTab", 0));
    lowerHeight = (int) v.getProperty ("lowerHeight", lowerHeight);
    c.zoneTab = (ZoneTab) jlimit (0, 4, (int) v.getProperty ("zoneTab", 0));
    c.editor = (EditorKind) jlimit (0, 4, (int) v.getProperty ("editor", 0));
    c.showChordLane = (bool) v.getProperty ("chordLane", c.showChordLane);
    c.showTempoLane = (bool) v.getProperty ("tempoLane", c.showTempoLane);
    c.showSectionLane = (bool) v.getProperty ("sectionLane", c.showSectionLane);
    c.followPlayback = (bool) v.getProperty ("follow", c.followPlayback);
    const double zoom = (double) v.getProperty ("zoom", 0.0);
    if (zoom > 0) arrange.timeline.pixelsPerTick = zoom;
    setTextSize ((float) (double) v.getProperty ("textSize", (double) theme::scale()));
    c.viewChanged();
    c.say ("The workspace " + f.getFileNameWithoutExtension().quoted() + ".");
    return true;
}

void MainComponent::importFiles()
{
    chooser = std::make_unique<FileChooser> ("Import", File::getSpecialLocation (File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.mid;*.midi");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
                          [this] (const FileChooser& fc)
    {
        Id track = c.focusTrack;
        for (auto& f : fc.getResults()) { c.importFile (f, track, c.cursor); track = 0; }
    });
}

void MainComponent::registerKeys()
{
    using K = KeyPress;
    const auto cmd = ModifierKeys::commandModifier, shift = ModifierKeys::shiftModifier;
    auto& keys = c.keys;
    auto zone = [this] (ZoneTab t) { return [this, t] { c.zoneTab = t; c.viewChanged(); }; };
    keys.add ("play", "Play or Stop", K (K::spaceKey), [this] { c.togglePlay(); });
    keys.add ("start", "Go to the Start", K (K::returnKey), [this] { c.goToStart(); });
    keys.add ("record", "Record", K ('r'), [this] { c.record(); });
    keys.add ("loop", "Loop", K ('l'), [this] { c.toggleLoop(); });
    keys.add ("dismiss", "Let Go of the Choice", K (K::escapeKey), [this] { c.selectedNotes.clear(); c.selectedClips.clear(); c.dismiss(); });
    keys.add ("delete", "Delete", K (K::backspaceKey), [this] { c.deleteSelection(); });
    keys.add ("delete2", "Delete (Forward)", K (K::deleteKey), [this] { c.deleteSelection(); });
    keys.add ("undo", "Undo", K ('z', cmd, 0), [this] { c.undo(); });
    keys.add ("redo", "Redo", K ('z', cmd | shift, 0), [this] { c.redo(); });
    keys.add ("save", "Save", K ('s', cmd, 0), [this] { saveFile(); });
    keys.add ("saveAs", "Save As", K ('s', cmd | shift, 0), [this] { c.file = File(); saveFile(); });
    keys.add ("open", "Open", K ('o', cmd, 0), [this] { openFile(); });
    keys.add ("new", "New", K ('n', cmd, 0), [this] { checkSaved ([this] { c.newProject(); }); });
    keys.add ("addTrack", "Add Track", K ('t', cmd, 0), [this] { c.addTrack (TrackKind::Track); });
    keys.add ("addFolder", "Add Folder", K ('t', cmd | shift, 0), [this] { c.addTrack (TrackKind::Folder); });
    keys.add ("duplicate", "Duplicate", K ('d', cmd, 0), [this] { c.duplicateSelection(); });
    keys.add ("split", "Split at the Cursor", K ('e', cmd, 0), [this] { c.splitAtCursor(); });
    keys.add ("chooseAll", "Choose Every Note", K ('a', cmd, 0), [this] { c.selectAllNotes(); });
    keys.add ("browser", "Show or Hide the Browser", K ('b', cmd, 0), [this] { browserShown = ! browserShown; browser.setVisible (browserShown); resized(); });
    keys.add ("capture", "Capture MIDI", K ('r', cmd | shift, 0), [this] { c.captureMidi(); });
    keys.add ("learn", "MIDI Learn", K ('l', cmd, 0), [this] { c.learnLastTouched(); });
    keys.add ("bigger", "Bigger Text", K ('=', cmd, 0), [this] { setTextSize (jmin (2.0f, theme::scale() + 0.25f)); });
    keys.add ("smaller", "Smaller Text", K ('-', cmd, 0), [this] { setTextSize (jmax (1.0f, theme::scale() - 0.25f)); });
    keys.add ("actualSize", "Text at 100%", K ('0', cmd, 0), [this] { setTextSize (1.0f); });
    keys.add ("import", "Import Audio or MIDI", K ('i', cmd, 0), [this] { importFiles(); });
    keys.add ("exportMix", "Export the Mix", K ('e', cmd | shift, 0), [this] { exportAudio (false); });
    keys.add ("quantise", "Quantise", K ('q'), [this] { c.quantiseSelection(); });
    keys.add ("devices", "Devices", K ('d'), zone (ZoneTab::Devices));
    keys.add ("editor", "Editor", K ('e'), zone (ZoneTab::Editor));
    keys.add ("mixer", "Mixer", K ('x'), zone (ZoneTab::Mixer));
    keys.add ("launcher", "Clip Launcher", K ('g'), zone (ZoneTab::Launcher));
    keys.add ("meters", "Meters", K ('y'), zone (ZoneTab::Meters));
    keys.add ("mute", "Mute the Chosen Tracks", K ('m'), [this] { c.apply ("Mute", [this] (Project& p) { for (auto id : c.selectedTracks) if (auto* t = p.track (id)) t->mute = ! t->mute; }); });
    keys.add ("solo", "Solo the Chosen Tracks", K ('s'), [this] { c.apply ("Solo", [this] (Project& p) { for (auto id : c.selectedTracks) if (auto* t = p.track (id)) t->solo = ! t->solo; }); });
    keys.add ("fit", "Zoom to Fit", K ('f'), [this] { arrange.zoomToFit(); });
    keys.file = settingsStore().getUserSettings() ? settingsStore().getUserSettings()->getFile().getSiblingFile ("Keys.json") : File();
    keys.load();
}

//==============================================================================
void MainComponent::checkSaved (std::function<void()> then)
{
    if (! c.dirty) { then(); return; }
    auto* w = new AlertWindow ("Save the changes to this project?", "If you don't, they're lost.", MessageBoxIconType::NoIcon);
    w->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Don't Save", 2);
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    ui::present (w, ModalCallbackFunction::create ([this, then] (int r)
    {
        if (r == 1) saveFile (then);
        else if (r == 2) then();
    }), true);
}

void MainComponent::openFile()
{
    checkSaved ([this]
    {
        chooser = std::make_unique<FileChooser> ("Open a project", c.projectFolder(), "*.kalibre");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
        {
            if (fc.getResult().existsAsFile()) c.open (fc.getResult());
        });
    });
}

void MainComponent::saveFile (std::function<void()> then)
{
    if (c.file.existsAsFile())
    {
        if (c.save() && then) then();
        return;
    }
    chooser = std::make_unique<FileChooser> ("Save the project", c.projectFolder().getChildFile ("Untitled.kalibre"), "*.kalibre");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting, [this, then] (const FileChooser& fc)
    {
        if (fc.getResult() == File()) return;
        fc.getResult().getParentDirectory().createDirectory();
        if (c.saveAs (fc.getResult()) && then) then();
    });
}

void MainComponent::exportAudio (bool stems, double loudnessTarget)
{
    chooser = std::make_unique<FileChooser> (stems ? "Export every track" : "Export the mix",
                                             c.projectFolder().getChildFile ((c.file.existsAsFile() ? c.file.getFileNameWithoutExtension() : String ("Mix")) + ".wav"),
                                             "*.wav;*.aiff;*.flac;*.mp3");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting, [this, stems, loudnessTarget] (const FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == File()) return;
        Engine::RenderOptions o;
        // The loop range when it's on, otherwise the whole song.
        const auto& p = c.project;
        o.start = p.loop.on ? p.tempo.secondsAt (p.loop.start) : 0.0;
        o.end = p.loop.on ? p.tempo.secondsAt (p.loop.end) : p.tempo.secondsAt (p.endTick());
        o.format = f.getFileExtension().trimCharactersAtStart (".").toLowerCase();
        if (o.format != "aiff" && o.format != "flac" && o.format != "mp3") o.format = "wav";
        if (o.format == "flac") o.bitDepth = 24;
        o.loudnessTarget = loudnessTarget;
        c.say ("Exporting" + String (ui::u8 ("\xe2\x80\xa6")));
        c.exportAudio (f, o, stems);
    });
}

void MainComponent::exportMidi()
{
    chooser = std::make_unique<FileChooser> ("Export MIDI", c.projectFolder().getChildFile ("Song.mid"), "*.mid");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting, [this] (const FileChooser& fc)
    {
        if (fc.getResult() != File()) c.exportMidi (fc.getResult().withFileExtension (".mid"));
    });
}

// Several exports in one go (decision 0041): chosen with ticks, into a folder.
void MainComponent::exportBatch()
{
    auto* w = new AlertWindow ("Export Several at Once", "Into one folder, named after the song. The loop range when it's on, otherwise the whole song.", MessageBoxIconType::NoIcon);
    auto tick = [w] (const String& name, bool on)
    {
        auto* b = new ToggleButton (name);
        b->setToggleState (on, dontSendNotification);
        b->setSize (300, 24);
        w->addCustomComponent (b);
        return b;
    };
    auto* wav = tick ("The Mix as WAV", true);
    auto* mp3 = tick ("The Mix as MP3 (320 kb/s)", true);
    auto* stems = tick ("Every Track as WAV", false);
    auto* midi = tick ("The MIDI", false);
    auto* loud = tick ("The Mix at -14 LUFS (for streaming)", false);
    w->addButton (ui::u8 ("Choose the Folder\xe2\x80\xa6"), 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    ui::present (w, ModalCallbackFunction::create ([this, w, wav, mp3, stems, midi, loud] (int r)
    {
        Controller::Batch b;
        b.mixWav = wav->getToggleState();
        b.mixMp3 = mp3->getToggleState();
        b.stems = stems->getToggleState();
        b.midi = midi->getToggleState();
        b.loudness = loud->getToggleState() ? -14.0 : 0.0;
        for (int i = w->getNumCustomComponents(); --i >= 0;) delete w->getCustomComponent (i);
        if (r != 1) return;
        chooser = std::make_unique<FileChooser> ("Choose where the exports go", c.projectFolder());
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [this, b] (const FileChooser& fc)
        {
            if (! fc.getResult().isDirectory()) return;
            const String song = c.file.existsAsFile() ? c.file.getFileNameWithoutExtension() : String ("Mix");
            c.say ("Exporting" + String (ui::u8 ("\xe2\x80\xa6")));
            c.exportBatch (fc.getResult().getChildFile (song + " " + Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M")), b);
        });
    }), true);
}

// The song's details, written into every export (decision 0041).
void MainComponent::songDetails()
{
    const auto& m = c.project.metadata;
    auto* w = new AlertWindow ("Song Details", "Written into exported WAV and MP3 files.", MessageBoxIconType::NoIcon);
    w->addTextEditor ("title", String (m.title), "Title");
    w->addTextEditor ("artist", String (m.artist), "Artist");
    w->addTextEditor ("album", String (m.album), "Album");
    w->addTextEditor ("year", m.year > 0 ? String (m.year) : String(), "Year");
    w->addTextEditor ("track", m.trackNumber > 0 ? String (m.trackNumber) : String(), "Track Number");
    w->addTextEditor ("genre", String (m.genre), "Genre");
    w->addTextEditor ("isrc", String (m.isrc), "ISRC");
    w->addTextEditor ("comment", String (m.comment), "Comment");
    w->addButton ("Keep", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    ui::present (w, ModalCallbackFunction::create ([this, w] (int r)
    {
        if (r != 1) return;
        Metadata m;
        m.title = w->getTextEditorContents ("title").trim().toStdString();
        m.artist = w->getTextEditorContents ("artist").trim().toStdString();
        m.album = w->getTextEditorContents ("album").trim().toStdString();
        m.year = w->getTextEditorContents ("year").getIntValue();
        m.trackNumber = w->getTextEditorContents ("track").getIntValue();
        m.genre = w->getTextEditorContents ("genre").trim().toStdString();
        m.isrc = w->getTextEditorContents ("isrc").trim().toStdString();
        m.comment = w->getTextEditorContents ("comment").trim().toStdString();
        c.apply ("Song Details", [m] (Project& p) { p.metadata = m; });
    }), true);
}

void MainComponent::showShortcuts()
{
    c.say ("Space play/stop " + ui::u8 ("\xc2\xb7") + " R record " + ui::u8 ("\xc2\xb7") + " L loop " + ui::u8 ("\xc2\xb7")
           + " Return to start " + ui::u8 ("\xc2\xb7") + " Q quantise " + ui::u8 ("\xc2\xb7") + " D/E/X devices, editor, mixer "
           + ui::u8 ("\xc2\xb7") + " F fit " + ui::u8 ("\xc2\xb7") + " M/S mute, solo " + ui::u8 ("\xc2\xb7") + " Cmd-Shift-R capture MIDI");
}

} // namespace kal
