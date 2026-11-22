#include "MainComponent.h"
#include "FilePanels.h"

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

MainComponent::MainComponent (Controller& ctrl) : c (ctrl), top (ctrl), transport (ctrl), arrange (ctrl), lower (ctrl), browser (ctrl), surfaces (ctrl)
{
    ui::setHost (this);
    ui::setQuestionsBelow (&top);
    ui::placeFilePanelsAtTop();   // Open and Save near the top, over plug-ins' windows
    // The top bar's timer (0091): the mouse moving or clicking in any of Kalibre's
    // windows is someone there; over another app, it isn't.
    Desktop::getInstance().addGlobalMouseListener (&activity);
    addAndMakeVisible (top);
    addAndMakeVisible (transport);
    transport.onMenu = [this] { top.showMenu(); };
    c.onRandomSong = [this] { randomSong(); };
    c.soundsIndex = [this] { return &library().scanner().index(); };
    addAndMakeVisible (arrange);
    addAndMakeVisible (lower);
    addChildComponent (browser);
   #if ! JUCE_MAC
    menuBar = std::make_unique<MenuBarComponent> (this);
    addAndMakeVisible (*menuBar);
   #endif
    if (auto* s = settingsStore().getUserSettings())
    {
        lowerHeight = s->getIntValue ("lowerHeight", defaultLowerHeight);
        lowerShown = s->getBoolValue ("lowerShown", true);
        browserWidth = s->getIntValue ("browserWidth", defaultBrowserWidth);
        browserShown = s->getBoolValue ("browser", false);
        c.zeroCrossings = s->getBoolValue ("zeroCrossings", false);
        c.workClock.setAwayMinutes (s->getIntValue ("awayMinutes", 3));
        c.hiddenInTopBar.addTokens (s->getValue ("hiddenInTopBar"), "|", {});
        c.hiddenInTopBar.removeEmptyStrings();
        c.cpuEverySeconds = jlimit (1, 5, s->getIntValue ("cpuEverySeconds", 2));
        c.arrangeGrid = jmax<Tick> (PPQ / 32, (Tick) s->getIntValue ("arrangeGrid", PPQ / 4));
        c.followPages = s->getBoolValue ("followPages", false);
        c.engine.setParallel (s->getBoolValue ("everyCore", true));   // decision 0078
        setTextSize ((float) s->getDoubleValue ("textSize", 1.0));
    }
    browser.setVisible (browserShown);
    lower.setVisible (lowerShown);
    shownZoneTab = c.zoneTab;
    setWantsKeyboardFocus (true);
    setTitle ("Kalibre");
    registerKeys();
    c.onShowPluginManager = [this] { showPluginManager(); };
    c.onShowArticulationManager = [this] { showArticulationManager(); };
    browser.library().onEditFolders = [this] { showLibraryFolders(); };
    c.addChangeListener (this);
    setSize (1440, 900);
}

MainComponent::~MainComponent()
{
    Desktop::getInstance().removeGlobalMouseListener (&activity);
    c.onRandomSong = nullptr;
    c.soundsIndex = nullptr;
    c.keys.all().clear();   // their actions point at this window
    c.onShowPluginManager = nullptr;
    c.onShowArticulationManager = nullptr;
    pluginWindow.reset();
    articulationWindow.reset();
    libraryWindow.reset();
    c.removeChangeListener (this);
    if (auto* s = settingsStore().getUserSettings())
    {
        s->setValue ("lowerHeight", lowerHeight);
        s->setValue ("lowerShown", lowerShown);
        s->setValue ("browserWidth", browserWidth);
        s->setValue ("browser", browserShown);
        s->setValue ("zeroCrossings", c.zeroCrossings);
        s->setValue ("awayMinutes", (int) std::lround (c.workClock.awayAfter / 60.0));
        s->setValue ("hiddenInTopBar", c.hiddenInTopBar.joinIntoString ("|"));
        s->setValue ("cpuEverySeconds", c.cpuEverySeconds);
        s->setValue ("arrangeGrid", (int) c.arrangeGrid);
        s->setValue ("followPages", c.followPages);
        s->setValue ("everyCore", c.engine.isParallel());
        s->setValue ("textSize", (double) theme::scale());
        settingsStore().saveIfNeeded();
        settingsStore().closeFiles();
    }
    ui::setHost (nullptr);
    ui::setQuestionsBelow (nullptr);
}

void MainComponent::changeListenerCallback (ChangeBroadcaster*)
{
    // Shut, the lower zone opens when one of its tabs is asked for (a key, a menu, a new plug-in).
    if (c.zoneTab != shownZoneTab)
    {
        shownZoneTab = c.zoneTab;
        if (! lowerShown) showLowerZone();
    }
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

void MainComponent::showLibraryFolders()
{
    if (! libraryWindow)
    {
        libraryWindow = std::make_unique<LibraryFoldersWindow> (browser.library());
        if (auto* top = getTopLevelComponent()) libraryWindow->centreAroundComponent (top, libraryWindow->getWidth(), libraryWindow->getHeight());
    }
    if (auto* view = dynamic_cast<LibraryFoldersView*> (libraryWindow->getContentComponent())) view->refresh();
    libraryWindow->setVisible (true);
    libraryWindow->toFront (true);
}

void MainComponent::firstRun()
{
    // Which folders to look in for sounds, asked once (decision 0094).
    browser.library().askOnFirstRun ([this] { showBrowser (Browser::Tab::Files); });
}

void MainComponent::showArticulationManager()
{
    if (! articulationWindow)
    {
        articulationWindow = std::make_unique<ArticulationManagerWindow> (c);
        if (auto* top = getTopLevelComponent()) articulationWindow->centreAroundComponent (top, articulationWindow->getWidth(), articulationWindow->getHeight());
    }
    if (auto* view = dynamic_cast<ArticulationManagerView*> (articulationWindow->getContentComponent())) view->refresh();
    articulationWindow->setVisible (true);
    articulationWindow->toFront (true);
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
    // Shut, the zone leaves a bar along the bottom to open it again from (0094).
    if (! lowerShown) return { arrange.getX(), transport.getY() - theme::px (6), getWidth() - arrange.getX(), theme::px (6) };
    return { lower.getX(), lower.getY() - theme::px (6), lower.getWidth(), theme::px (6) };
}

void MainComponent::showLowerZone()
{
    lowerShown = true;
    lower.setVisible (true);
    resized();
    repaint();
}

void MainComponent::hideLowerZone()
{
    lowerShown = false;
    lower.setVisible (false);
    resized();
    repaint();
}

void MainComponent::paint (Graphics& g)
{
    g.fillAll (theme::ground);
    const auto d = dividerArea();
    if (! lowerShown)
    {
        g.setColour (theme::sunken);
        g.fillRect (d);
        g.setColour (theme::rule);
        g.fillRect (d.withHeight (1));
    }
    else
    {
        g.setColour (draggingDivider ? theme::control : theme::rule);
        g.fillRect (d.withSizeKeepingCentre (d.getWidth(), 2));
    }
    g.setColour (draggingDivider ? theme::control : theme::edge);
    for (int i = -1; i <= 1; ++i)
        g.fillEllipse (Rectangle<float> (3.0f, 3.0f).withCentre (d.getCentre().toFloat().translated ((float) i * 6.0f, 0.0f)));
    // The browser's handle: three dots down its edge, or, shut, down the bar at the left.
    const auto h = browserHandle();
    if (! browserShown)
    {
        g.setColour (theme::sunken);
        g.fillRect (h);
        g.setColour (theme::rule);
        g.fillRect (h.withLeft (h.getRight() - 1));
    }
    g.setColour (draggingBrowserEdge ? theme::control : theme::edge);
    for (int i = -1; i <= 1; ++i)
        g.fillEllipse (Rectangle<float> (3.0f, 3.0f).withCentre (browserHandle().getCentre().toFloat().translated (0.0f, (float) i * 6.0f)));
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    if (menuBar) menuBar->setBounds (r.removeFromTop (theme::px (24)));
    top.setBounds (r.getX(), r.getY(), r.getWidth(), 1);
    top.setBounds (r.removeFromTop (top.preferredHeight()));
    // The transport, along the bottom under everything (asked for, 0095).
    transport.setBounds (r.removeFromBottom (transport.preferredHeight()));
    if (browserShown)
    {
        // Its width is the musician's: drag its edge (decision 0094).
        browser.setBounds (r.removeFromLeft (jlimit (theme::px (240), jmax (theme::px (240), r.getWidth() / 2), browserWidth)));
        r.removeFromLeft (theme::px (5));
    }
    else r.removeFromLeft (theme::px (6));   // the bar it's opened again from
    // The zone keeps the height it was given; only what's shown is limited by the window.
    if (lowerShown)
    {
        const int zone = jlimit (theme::px (120), jmax (theme::px (120), r.getHeight() - theme::px (160)), lowerHeight);
        lower.setBounds (r.removeFromBottom (zone));
    }
    r.removeFromBottom (theme::px (6));   // the divider, or shut, the bar
    arrange.setBounds (r);
}

Rectangle<int> MainComponent::browserHandle() const
{
    const int y = top.getBottom();
    if (! browserShown) return { 0, y, theme::px (6), transport.getY() - y };
    return { browser.getRight(), browser.getY(), theme::px (5), browser.getHeight() };
}

void MainComponent::mouseMove (const MouseEvent& e)
{
    setMouseCursor (dividerArea().expanded (0, 2).contains (e.getPosition()) ? MouseCursor::UpDownResizeCursor
                    : browserHandle().contains (e.getPosition()) ? MouseCursor::LeftRightResizeCursor : MouseCursor::NormalCursor);
}

void MainComponent::mouseDown (const MouseEvent& e)
{
    draggingDivider = dividerArea().expanded (0, 2).contains (e.getPosition());
    draggingBrowserEdge = ! draggingDivider && browserHandle().contains (e.getPosition());
    browserWasShown = browserShown;
    browserDragMoved = false;
    lowerWasShown = lowerShown;
    lowerDragMoved = false;
    dragStartHeight = lowerShown ? lower.getHeight() : 0;
    dragStartWidth = browserShown ? browser.getWidth() : 0;
}

void MainComponent::mouseUp (const MouseEvent&)
{
    // A click on the bar, not a drag, opens the browser as wide as it was.
    if (draggingBrowserEdge && ! browserDragMoved && ! browserWasShown)
    {
        browserShown = true;
        browser.setVisible (true);
        resized();
        repaint();
    }
    // The same for the lower zone's bar.
    if (draggingDivider && ! lowerDragMoved && ! lowerWasShown) showLowerZone();
    draggingBrowserEdge = false;
    draggingDivider = false;
    repaint();
}

// A double-click on the dots (the divider, or a shut panel's bar) opens that
// panel at its first size (0095): the way back from a drag gone too far.
void MainComponent::mouseDoubleClick (const MouseEvent& e)
{
    if (dividerArea().expanded (0, 2).contains (e.getPosition()))
    {
        lowerHeight = defaultLowerHeight;
        showLowerZone();
    }
    else if (browserHandle().contains (e.getPosition()))
    {
        browserWidth = defaultBrowserWidth;
        browserShown = true;
        browser.setVisible (true);
        resized();
        repaint();
    }
}

void MainComponent::mouseDrag (const MouseEvent& e)
{
    if (draggingBrowserEdge)
    {
        // It follows the drag down to its smallest, stops there, and past
        // half of that it shuts, leaving the bar; dragged from the bar, it opens.
        const int least = theme::px (240);
        const int wanted = dragStartWidth + e.getDistanceFromDragStartX();
        if (std::abs (e.getDistanceFromDragStartX()) > 3) browserDragMoved = true;
        if (wanted < least / 2)
        {
            browserShown = false;
            browser.setVisible (false);
        }
        else
        {
            browserWidth = jlimit (least, jmax (least, getWidth() / 2), wanted);
            browserShown = true;
            browser.setVisible (true);
        }
        resized();
        repaint();
        return;
    }
    if (! draggingDivider) return;
    // Every plug-in window in the zone follows its height, down to its smallest;
    // past half of that it shuts, leaving the bar; dragged up from the bar, it opens.
    const int least = theme::px (120);
    const int wanted = dragStartHeight - e.getDistanceFromDragStartY();
    if (std::abs (e.getDistanceFromDragStartY()) > 3) lowerDragMoved = true;
    if (wanted < least / 2)
    {
        lowerShown = false;
        lower.setVisible (false);
    }
    else
    {
        lowerHeight = jlimit (least, jmax (least, getHeight() - theme::px (300)), wanted);
        lowerShown = true;
        lower.setVisible (true);
    }
    resized();
    repaint();
}

//==============================================================================

bool MainComponent::keyPressed (const KeyPress& k)
{
    c.noteActivity();   // a key is someone there (0091)
    // Step input takes the numbers while it's on (decision 0076).
    if (c.stepInput && ! k.getModifiers().isCommandDown() && ! k.getModifiers().isCtrlDown() && ! k.getModifiers().isAltDown())
    {
        int digit = -1;
        const auto ch = k.getTextCharacter();
        if (ch >= '0' && ch <= '9') digit = (int) (ch - '0');
        else if (k.getKeyCode() >= '0' && k.getKeyCode() <= '9') digit = k.getKeyCode() - '0';
        else if (k.getKeyCode() >= KeyPress::numberPad0 && k.getKeyCode() <= KeyPress::numberPad9) digit = k.getKeyCode() - KeyPress::numberPad0;
        if (digit >= 0 && c.stepDigit (digit)) return true;
        // The - key (or the keypad's): a rest.
        if ((ch == '-' || k.getKeyCode() == '-' || k.getKeyCode() == KeyPress::numberPadSubtract) && c.stepRest()) return true;
    }
    // Every key is the user's to change (decision 0045), in Help > Keys...
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
    o->setProperty ("lowerShown", lowerShown);
    o->setProperty ("browserWidth", browserWidth);
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
    browser.showTab (Browser::tabFromNumber ((int) v.getProperty ("browserTab", 0)));
    lowerHeight = (int) v.getProperty ("lowerHeight", lowerHeight);
    lowerShown = (bool) v.getProperty ("lowerShown", true);
    lower.setVisible (lowerShown);
    browserWidth = (int) v.getProperty ("browserWidth", browserWidth);
    {
        // A workspace saved with the clip launcher showing (3, taken out, 0098) opens on Devices.
        const int tab = (int) v.getProperty ("zoneTab", 0);
        c.zoneTab = tab == 1 || tab == 2 || tab == 4 || tab == 5 ? (ZoneTab) tab : ZoneTab::Devices;
    }
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
    auto zone = [this] (ZoneTab t) { return [this, t] { c.zoneTab = t; shownZoneTab = t; showLowerZone(); c.viewChanged(); }; };
    keys.add ("play", "Play or Stop", K (K::spaceKey), [this] { c.togglePlay(); });
    keys.add ("start", "Go to the Start", K (K::returnKey), [this] { c.goToStart(); });
    keys.add ("end", "Go to the End", K (K::endKey), [this] { c.goToEnd(); });
    keys.add ("stopStart", "Stop and Go to the Start", K (K::homeKey), [this] { c.stopAndGoToStart(); });
    keys.add ("record", "Record", K ('r'), [this] { c.record(); });
    keys.add ("loop", "Loop", K ('l'), [this] { c.toggleLoop(); });
    keys.add ("dismiss", "Let Go of the Choice", K (K::escapeKey), [this] { c.selectedNotes.clear(); c.selectedClips.clear(); c.range = {}; c.dismiss(); });
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
    keys.add ("lowerZone", "Show or Hide the Lower Zone", K(), [this] { if (lowerShown) hideLowerZone(); else showLowerZone(); });
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
    keys.add ("meters", "Meters", K ('y'), zone (ZoneTab::Meters));
    keys.add ("mute", "Mute the Chosen Tracks", K ('m'), [this] { c.apply ("Mute", [this] (Project& p) { for (auto id : c.selectedTracks) if (auto* t = p.track (id)) t->mute = ! t->mute; }); });
    keys.add ("solo", "Solo the Chosen Tracks", K ('s'), [this] { c.apply ("Solo", [this] (Project& p) { for (auto id : c.selectedTracks) if (auto* t = p.track (id)) t->solo = ! t->solo; }); });
    keys.add ("fit", "Zoom to Fit", K ('f'), [this] { arrange.zoomToFit(); });
    // The tools (decision 0092), in Cubase's order on 1 to 9; the rest without a key until given one.
    for (int i = 0; i < tools::toolCount; ++i)
    {
        const auto tool = (tools::Tool) i;
        const String name (tools::info (tool).name);
        keys.add ("tool" + name.removeCharacters (" "), "Tool: " + name, i < 9 ? K ((juce_wchar) ('1' + i)) : K(),
                  [this, tool] { c.chooseToolEverywhere (tool); });
    }
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

// Record the Tail and Export the Second Pass (0097): asked before every export
// of audio, remembered for the next.
static ToggleButton* exportTick (AlertWindow* w, const String& name, bool on)
{
    auto* b = new ToggleButton (name);
    b->setName ({});   // the window would print its name again above it
    b->setTitle (name);
    b->setToggleState (on, dontSendNotification);
    b->setSize (420, 24);
    w->addCustomComponent (b);
    return b;
}

static const char* tailHelp = "Record the Tail: after the end nothing new plays, and what's still sounding (releases, reverbs, delays) "
                              "is recorded until it falls silent. Off: the file stops on the end.";
static const char* passHelp = "Export the Second Pass: the range is played through once unheard, then recorded the second time, "
                              "so what rings past its end is heard at its start: a loop that loops seamlessly.";

void MainComponent::exportAudio (bool stems, double loudnessTarget)
{
    auto* s = settingsStore().getUserSettings();
    const bool tailWas = s ? s->getBoolValue ("exportTail", true) : true;
    const bool passWas = s ? s->getBoolValue ("exportSecondPass", false) : false;
    auto* w = new AlertWindow (stems ? "Export Every Track" : "Export the Mix",
                               String (c.project.loop.on ? "The loop range." : "The whole song.") + "\n\n" + tailHelp + "\n\n" + passHelp,
                               MessageBoxIconType::NoIcon);
    auto* tail = exportTick (w, "Record the Tail", tailWas);
    auto* pass = exportTick (w, "Export the Second Pass", passWas);
    w->addButton (ui::u8 ("Choose Where\xe2\x80\xa6"), 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    ui::present (w, ModalCallbackFunction::create ([this, w, tail, pass, stems, loudnessTarget] (int r)
    {
        const bool ringOut = tail->getToggleState(), secondPass = pass->getToggleState();
        for (int i = w->getNumCustomComponents(); --i >= 0;) delete w->getCustomComponent (i);
        if (r != 1) return;
        if (auto* st = settingsStore().getUserSettings()) { st->setValue ("exportTail", ringOut); st->setValue ("exportSecondPass", secondPass); }
        exportAudioTo (stems, loudnessTarget, ringOut, secondPass);
    }), true);
}

void MainComponent::exportAudioTo (bool stems, double loudnessTarget, bool ringOut, bool secondPass)
{
    chooser = std::make_unique<FileChooser> (stems ? "Export every track" : "Export the mix",
                                             c.projectFolder().getChildFile ((c.file.existsAsFile() ? c.file.getFileNameWithoutExtension() : String ("Mix")) + ".wav"),
                                             "*.wav;*.aiff;*.flac;*.mp3");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting,
                          [this, stems, loudnessTarget, ringOut, secondPass] (const FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == File()) return;
        Engine::RenderOptions o;
        o.tail = 0;
        o.ringOut = ringOut;
        o.secondPass = secondPass;
        // The loop range when it's on, otherwise the whole song.
        const auto& p = c.project;
        o.start = p.loop.on ? p.tempo.secondsAt (p.loop.start) : 0.0;
        o.end = p.loop.on ? p.tempo.secondsAt (p.loop.end) : p.tempo.secondsAt (p.songEnd());
        o.format = f.getFileExtension().trimCharactersAtStart (".").toLowerCase();
        if (o.format != "aiff" && o.format != "flac" && o.format != "mp3") o.format = "wav";
        if (o.format == "flac") o.bitDepth = 24;
        o.loudnessTarget = loudnessTarget;
        c.say ("Exporting" + String (ui::u8 ("\xe2\x80\xa6")));
        c.exportAudio (f, o, stems);
    });
}

// The studio and the keys: settings windows of their own (they were the browser's tabs).
StudioWindow& MainComponent::showStudio()
{
    if (! studioWindow)
    {
        studioWindow = std::make_unique<StudioWindow> (c);
        if (auto* top = getTopLevelComponent()) studioWindow->centreAroundComponent (top, studioWindow->getWidth(), studioWindow->getHeight());
    }
    studioWindow->setVisible (true);
    studioWindow->toFront (true);
    return *studioWindow;
}

void MainComponent::showAgents()
{
    showStudio().showAgents();
}

KeysWindow& MainComponent::showKeys()
{
    if (! keysWindow)
    {
        keysWindow = std::make_unique<KeysWindow> (c.keys);
        if (auto* top = getTopLevelComponent()) keysWindow->centreAroundComponent (top, keysWindow->getWidth(), keysWindow->getHeight());
    }
    keysWindow->refresh();
    keysWindow->setVisible (true);
    keysWindow->toFront (true);
    return *keysWindow;
}

// The Random button (decision 0101): the song as it is kept or let go of, then a new one by chance.
void MainComponent::randomSong()
{
    checkSaved ([this]
    {
        c.randomSong (library().scanner().index(), (unsigned) Random::getSystemRandom().nextInt (1 << 30) + 1u);
        showLowerZone();
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
        b->setName ({});   // the window would print its name again above it
        b->setTitle (name);
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
    auto* settings = settingsStore().getUserSettings();
    auto* tail = exportTick (w, "Record the Tail", settings ? settings->getBoolValue ("exportTail", true) : true);
    auto* pass = exportTick (w, "Export the Second Pass", settings ? settings->getBoolValue ("exportSecondPass", false) : false);
    w->addButton (ui::u8 ("Choose the Folder\xe2\x80\xa6"), 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    ui::present (w, ModalCallbackFunction::create ([this, w, wav, mp3, stems, midi, loud, tail, pass] (int r)
    {
        Controller::Batch b;
        b.ringOut = tail->getToggleState();
        b.secondPass = pass->getToggleState();
        if (r == 1)
            if (auto* st = settingsStore().getUserSettings()) { st->setValue ("exportTail", b.ringOut); st->setValue ("exportSecondPass", b.secondPass); }
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
