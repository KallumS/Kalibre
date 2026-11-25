// Kalibre: the app, its one window, and opening projects from the Finder.

#include "CrashReports.h"
#include "MainComponent.h"
#include "MainWindow.h"
#include "McpServer.h"
#include <iostream>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace kal
{
using namespace juce;

class Application : public JUCEApplication
{
public:
    const String getApplicationName() override { return "Kalibre"; }
    const String getApplicationVersion() override { return KALIBRE_VERSION; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const String& commandLine) override
    {
        // A crash leaves a report of where (decision 0073); the last start's is told of below.
        const auto reports = crash::folder();
        const auto seenMarker = reports.getChildFile ("Shown.txt");
        const auto unread = crash::since (reports, seenMarker.existsAsFile() ? seenMarker.getLastModificationTime() : Time());
        crash::install (reports, getApplicationVersion());
        LookAndFeel::setDefaultLookAndFeel (&look);
        // What a control does, shown when the pointer rests on it, in every window.
        tips = std::make_unique<TooltipWindow> (nullptr, 600);
        plugins = std::make_unique<PluginManager>();
        engine = std::make_unique<Engine> (*plugins);
        const auto error = engine->startAudio();
        controller = std::make_unique<Controller> (*engine);
        window = std::make_unique<MainWindow> (*controller);
        window->content->startBattlePass();   // unseen until its secret (0111)
       #if JUCE_MAC
        window->extraAppMenu = window->content->appMenu();
        MenuBarModel::setMacMainMenu (window->content, &window->extraAppMenu);
       #endif
        if (error.isNotEmpty()) controller->say ("The audio device didn't start: " + error + " Choose one under Audio in the browser.", true);
        else if (plugins->known().getNumTypes() == 0)
            controller->say ("Welcome. Kalibre hasn't looked for your plug-ins yet: choose Plug-in Manager from " + String (ui::words().settingsMenu) + " and click Scan for New Plug-ins.");
        else if (plugins->needingAnotherLook() > 0)
            controller->say ("Kalibre now opens each plug-in as it scans, to learn which send MIDI (Riffer, Scaler) and file them as MIDI effects. "
                             "Choose Plug-in Manager from " + String (ui::words().settingsMenu) + " and click Scan for New Plug-ins: it looks at yours again.");
        else if (! plugins->blocked().empty())
        {
            // Left by the old scanner, which crashed with the plug-in (decision 0047).
            int crashed = 0;
            for (auto& b : plugins->blocked()) crashed += b.detail == "it closed Kalibre while being scanned" ? 1 : 0;
            if (crashed > 0)
                controller->say ("A plug-in closed Kalibre while it was being scanned. It's turned off now, and scans look at each plug-in "
                                 "in a helper, so a crash can't close Kalibre again. " + String (platform::inSettings ("Plug-in Manager")) + " shows it.");
        }
        if (! unread.isEmpty())
        {
            controller->say ("Kalibre quit unexpectedly last time (" + crash::summary (unread.getFirst()) + "). Help > Show Crash Reports has the report; "
                             "File > Open a Backup has your song from up to 15 minutes before.", true);
            seenMarker.replaceWithText (unread.getFirst().getFileName());
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
// Kalibre's start: the app, or the scanner's helper, or the MCP bridge. Called by
// main, and on Windows by WinMain (MainWin.cpp), with the arguments in UTF-8.
int kalibreMain (int argc, char* argv[])
{
    if (argc > 1 && std::strcmp (argv[1], kal::PluginManager::workerFlag) == 0)
        return kal::PluginManager::runWorker (argc, argv);
    // An AI agent's MCP server on stdin and stdout, passing on to the open Kalibre (decision 0099).
    if (argc > 1 && std::strcmp (argv[1], kal::McpServer::bridgeFlag) == 0)
        return kal::runMcpBridge (std::cin, std::cout);
    juce::JUCEApplicationBase::createInstance = [] () -> juce::JUCEApplicationBase* { return new kal::Application(); };
   #if JUCE_WINDOWS
    return juce::JUCEApplicationBase::main();   // Windows' takes its arguments from the system
   #else
    return juce::JUCEApplicationBase::main (argc, (const char**) argv);
   #endif
}

#if ! JUCE_WINDOWS
int main (int argc, char* argv[]) { return kalibreMain (argc, argv); }
#endif
