/*
    MainComponent - the one window (decision 0002).

        ┌ menus in the Mac's menu bar (decision 0027) ──────────────────────┐
        │ transport · position · tempo · time · scale · CHORD               │
        │ message line (only when there is something to say)               │
        ├──────────┬────────────────────────────────────────────────────────┤
        │ browser  │ ruler · chord lane · tempo lane                        │
        │ (View >  │ track headers │ clips and automation                   │
        │ Browser) │                                                        │
        │          ├──────────────── divider (drag) ───────────────────────┤
        │          │ Devices · Editor · Mixer                               │
        └──────────┴────────────────────────────────────────────────────────┘

    Keys are the window's own (keyPressed): Space plays, R records, and the
    rest are listed in the menus beside their commands.
*/

#pragma once

#include "Arrange.h"
#include "Browser.h"
#include "Controller.h"
#include "LowerZone.h"
#include "PluginManagerView.h"
#include "ArticulationManagerView.h"
#include "LibraryFolders.h"
#include "SettingsWindows.h"
#include "BattlePass.h"
#include "Surfaces.h"
#include "TopBar.h"

namespace kal
{

// Tells the Controller when the mouse does anything in Kalibre (the top bar's timer, 0091).
struct ActivityWatch : public juce::MouseListener
{
    explicit ActivityWatch (Controller& ctrl) : c (ctrl) {}
    Controller& c;
    // As the battle pass does with js_ReaScriptAPI: only while Kalibre is the app
    // in front. JUCE tells a global listener of every move of the pointer, even
    // over another app's window lying on top of Kalibre's, which kept the timer
    // running while the musician was elsewhere.
    static bool inFront() { return juce::Process::isForegroundProcess(); }
    void mouseMove (const juce::MouseEvent&) override { if (inFront()) c.noteActivity(); }
    void mouseDown (const juce::MouseEvent&) override { if (inFront()) c.noteActivity(); }
    void mouseDrag (const juce::MouseEvent&) override { if (inFront()) c.noteActivity(); }
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override { if (inFront()) c.noteActivity(); }
};

class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      public juce::MenuBarModel,
                      private juce::ChangeListener
{
public:
    explicit MainComponent (Controller&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    // The sizes a double-click on a handle's dots brings back (0095).
    static constexpr int defaultLowerHeight = 320, defaultBrowserWidth = 320;

    // MenuBarModel (Menus.cpp): File, Edit, Track, Transport, Automation, View, Scripts, Help.
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int, int) override {}
    juce::PopupMenu appMenu();
    // About Kalibre: its version (1.0.0 and on; every change goes up, docs/CHANGES.md).
    static juce::String aboutText();
    static void showAbout();

    // Asks before throwing away unsaved work; `then` runs if it's fine to go on.
    void checkSaved (std::function<void()> then);
    void openFile();
    void saveFile (std::function<void()> then = {});
    // Asks Record the Tail and Export the Second Pass (0097), then where the file goes.
    void exportAudio (bool stems, double loudnessTarget = 0);
    void exportAudioTo (bool stems, double loudnessTarget, bool ringOut, bool secondPass);
    void exportMidi();
    void exportBatch();
    void songDetails();
    void setTextSize (float);
    void setTheme (int index);   // View > Themes (decision 0132)
    void showBrowser (Browser::Tab);
    StudioWindow& showStudio();   // File > Studio...: sync, control surfaces, OSC, AI agents
    void showAgents();            // the Studio window at AI Agents (0099)
    KeysWindow& showKeys();       // Help > Keys...: every command and its key (0045)
    void randomSong (bool addTo = false);   // the top bar's Random: a new song by chance (0101); Shift: tracks added (0119)
    void hideBrowser() { browserShown = false; browser.setVisible (false); resized(); }
    void showPluginManager();
    void showArticulationManager();
    void showLibraryFolders();
    LibraryView& library() { return browser.library(); }
    // What to drag to widen, narrow, shut or open the browser (0094): its right
    // edge while it's shown; shut, a thin bar at the window's left edge.
    juce::Rectangle<int> browserHandle() const;
    juce::Rectangle<int> browserBounds() const { return browser.getBounds(); }
    // The same for the lower zone: the divider above it while it's shown; shut, a bar along the bottom.
    juce::Rectangle<int> lowerHandle() const { return dividerArea(); }
    juce::Rectangle<int> lowerBounds() const { return lower.getBounds(); }
    juce::Rectangle<int> transportBounds() const { return transport.getBounds(); }
    TransportBar& transportBar() { return transport; }
    TopBar& topBar() { return top; }
    void showLowerZone();
    void hideLowerZone();
    bool lowerShown = true;
    void firstRun();   // after the window first shows: the questions asked once
    // DAW Battle Pass (0111): started by the app (not by tests or renders), running unseen;
    // the timer's seven clicks show it.
    void startBattlePass (int afterMs = 3000);

    Controller& controller() { return c; }
    bool browserShown = false;
    int lowerHeight = 320;
    int browserWidth = 320;

private:
    Controller& c;
    ActivityWatch activity { c };
    TopBar top;
    TransportBar transport;   // along the bottom, under the lower zone (0095)
    Arrange arrange;
    LowerZone lower;
    Browser browser;
    Surfaces surfaces;
    std::unique_ptr<juce::MenuBarComponent> menuBar;   // on Linux and Windows; the Mac uses its own
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<PluginManagerWindow> pluginWindow;
    std::unique_ptr<ArticulationManagerWindow> articulationWindow;
    std::unique_ptr<LibraryFoldersWindow> libraryWindow;
    std::unique_ptr<StudioWindow> studioWindow;
    std::unique_ptr<KeysWindow> keysWindow;
public:
    std::unique_ptr<BattlePass> battlePass;   // DAW Battle Pass, running unseen (0111)
private:
    int dragStartHeight = 0;
    bool draggingDivider = false, draggingBrowserEdge = false, browserDragMoved = false, browserWasShown = false;
    bool lowerDragMoved = false, lowerWasShown = true;
    ZoneTab shownZoneTab = ZoneTab::Devices;
    int dragStartWidth = 0;

    juce::Rectangle<int> dividerArea() const;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showShortcuts();
    void registerKeys();
public:
    // Workspaces (decision 0045): the window's layout, saved by name.
    juce::File workspacesFolder() const;
    bool saveWorkspace (const juce::String& name);
    bool loadWorkspace (const juce::File&);
private:
    void importFiles();
};

} // namespace kal
