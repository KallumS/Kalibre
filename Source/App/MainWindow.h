/*
    MainWindow - Kalibre's one window (decision 0002): the main component in it.
*/

#pragma once

#include "MainComponent.h"

namespace kal
{
using namespace juce;

class MainWindow : public DocumentWindow
{
public:
    explicit MainWindow (Controller& c, bool askFirstRun = true)
        : DocumentWindow ("Kalibre", theme::ground, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        content = new MainComponent (c);
        setContentOwned (content, true);
        setResizable (true, true);
        // The smallest the window goes: the transport and a track still fit.
        setResizeLimits (900, 560, 10000, 10000);
        centreWithSize (jmin (1440, Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.getWidth() - 40),
                        jmin (900, Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea.getHeight() - 40));
       #if JUCE_MAC
        MenuBarModel::setMacMainMenu (content, &extraAppMenu);
       #endif
        setVisible (true);
        Component::SafePointer<MainComponent> safe (content);
        if (askFirstRun) MessageManager::callAsync ([safe] { if (safe) safe->firstRun(); });
    }

    ~MainWindow() override
    {
       #if JUCE_MAC
        MenuBarModel::setMacMainMenu (nullptr);
       #endif
    }

    void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }

    // Keys that reach the window with nothing in it focused (after a dialog closes) go to
    // Kalibre's own: its shortcuts work without a click first (found in the UAT: ⇧⌘E did nothing).
    bool keyPressed (const KeyPress& k) override { return content != nullptr && content->keyPressed (k); }

    MainComponent* content = nullptr;
    PopupMenu extraAppMenu;
};

} // namespace kal
