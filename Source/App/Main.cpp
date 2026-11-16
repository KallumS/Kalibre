// Kalibre: the app, its one window, and opening projects from the Finder.

#include "MainComponent.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

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
        // What a control does, shown when the pointer rests on it, in every window.
        tips = std::make_unique<TooltipWindow> (nullptr, 600);
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
        else if (! plugins->blocked().empty())
        {
            // Left by the old scanner, which crashed with the plug-in (decision 0047).
            int crashed = 0;
            for (auto& b : plugins->blocked()) crashed += b.detail == "it closed Kalibre while being scanned" ? 1 : 0;
            if (crashed > 0)
                controller->say ("A plug-in closed Kalibre while it was being scanned. It's turned off now, and scans look at each plug-in "
                                 "in a helper, so a crash can't close Kalibre again. Kalibre > Plug-in Manager shows it.");
        }
        const auto file = File::getCurrentWorkingDirectory().getChildFile (commandLine.unquoted());
        if (commandLine.isNotEmpty() && file.hasFileExtension ("kalibre")) controller->open (file);
    }

    void shutdown() override
    {
        // Leaving can't wait on a plug-in or a driver that won't let go: the
        // project was saved or let go before this, so after five seconds the
        // process ends whatever is still unwinding. A Kalibre still running
        // after it was quit is also one the Mac reopens at login.
        std::thread ([] { std::this_thread::sleep_for (std::chrono::seconds (5)); std::_Exit (0); }).detach();
        window.reset();
        controller.reset();
        engine.reset();
        plugins.reset();
        tips.reset();
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
    std::unique_ptr<TooltipWindow> tips;
    std::unique_ptr<PluginManager> plugins;
    std::unique_ptr<Engine> engine;
    std::unique_ptr<Controller> controller;
    std::unique_ptr<MainWindow> window;
};

} // namespace kal

// Kalibre runs itself as the plug-in scanner's helper (decision 0047): that copy
// never starts the app, so it opens no window and asks for no audio device.
int main (int argc, char* argv[])
{
    if (argc > 1 && std::strcmp (argv[1], kal::PluginManager::workerFlag) == 0)
        return kal::PluginManager::runWorker (argc, argv);
    juce::JUCEApplicationBase::createInstance = [] () -> juce::JUCEApplicationBase* { return new kal::Application(); };
    return juce::JUCEApplicationBase::main (argc, (const char**) argv);
}
