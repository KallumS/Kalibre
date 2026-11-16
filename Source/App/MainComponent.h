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
#include "Surfaces.h"
#include "TopBar.h"

namespace kal
{

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

    // MenuBarModel (Menus.cpp): File, Edit, Track, Transport, Automation, View, Scripts, Help.
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int, const juce::String&) override;
    void menuItemSelected (int, int) override {}
    juce::PopupMenu appMenu();

    // Asks before throwing away unsaved work; `then` runs if it's fine to go on.
    void checkSaved (std::function<void()> then);
    void openFile();
    void saveFile (std::function<void()> then = {});
    void exportAudio (bool stems, double loudnessTarget = 0);
    void exportMidi();
    void exportBatch();
    void songDetails();
    void setTextSize (float);
    void showBrowser (Browser::Tab);
    void hideBrowser() { browserShown = false; browser.setVisible (false); resized(); }

    Controller& controller() { return c; }
    bool browserShown = false;
    int lowerHeight = 320;

private:
    Controller& c;
    TopBar top;
    Arrange arrange;
    LowerZone lower;
    Browser browser;
    Surfaces surfaces;
    std::unique_ptr<juce::MenuBarComponent> menuBar;   // on Linux and Windows; the Mac uses its own
    std::unique_ptr<juce::FileChooser> chooser;
    int dragStartHeight = 0;
    bool draggingDivider = false;

    juce::Rectangle<int> dividerArea() const;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showShortcuts();
};

} // namespace kal
