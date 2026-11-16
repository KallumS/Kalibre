// Kalibre: the app, its one window, and opening projects from the Finder.

#include "MainComponent.h"

namespace kal
{
using namespace juce;

class MainWindow : public DocumentWindow
{
public:
    MainWindow (Controller& c)
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
    }

    ~MainWindow() override
    {
       #if JUCE_MAC
        MenuBarModel::setMacMainMenu (nullptr);
       #endif
    }

    void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }

    MainComponent* content = nullptr;
    PopupMenu extraAppMenu;
};

class Application : public JUCEApplication
{
public:
    const String getApplicationName() override { return "Kalibre"; }
    const String getApplicationVersion() override { return KALIBRE_VERSION; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const String& commandLine) override
    {
        LookAndFeel::setDefaultLookAndFeel (&look);
        plugins = std::make_unique<PluginManager>();
        engine = std::make_unique<Engine> (*plugins);
        const auto error = engine->startAudio();
        controller = std::make_unique<Controller> (*engine);
        window = std::make_unique<MainWindow> (*controller);
       #if JUCE_MAC
        window->extraAppMenu = window->content->appMenu();
        MenuBarModel::setMacMainMenu (window->content, &window->extraAppMenu);
       #endif
        if (error.isNotEmpty()) controller->say ("The audio device didn't start: " + error + " Choose one under Audio in the browser.", true);
        else if (plugins->known().getNumTypes() == 0)
            controller->say ("Welcome. Kalibre hasn't looked for your plug-ins yet: choose Scan for Plug-ins from the Kalibre menu.");
        const auto file = File::getCurrentWorkingDirectory().getChildFile (commandLine.unquoted());
        if (commandLine.isNotEmpty() && file.hasFileExtension ("kalibre")) controller->open (file);
    }

    void shutdown() override
    {
        window.reset();
        controller.reset();
        engine.reset();
        plugins.reset();
        LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        if (! window) { quit(); return; }
        window->content->checkSaved ([] { JUCEApplication::getInstance()->quit(); });
    }

    void anotherInstanceStarted (const String& commandLine) override
    {
        const File f (commandLine.unquoted());
        if (f.hasFileExtension ("kalibre") && window)
            window->content->checkSaved ([this, f] { controller->open (f); });
    }

private:
    theme::LookAndFeel look;
    std::unique_ptr<PluginManager> plugins;
    std::unique_ptr<Engine> engine;
    std::unique_ptr<Controller> controller;
    std::unique_ptr<MainWindow> window;
};

} // namespace kal

START_JUCE_APPLICATION (kal::Application)
