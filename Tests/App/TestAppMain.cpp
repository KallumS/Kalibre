// The window's and the engine's tests: JUCE is up, no audio device is needed.
#include "Check.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginManager.h"
#include "CrashReports.h"
#include "CrashContext.h"

#include <cstring>

void installTestFormats();   // TestPlugins.cpp
#if JUCE_WINDOWS
void installTestCrashPrinter();   // TestCrashWin.cpp: how and where, if the tests end early (0129)
void testsFinished();
#endif

int main (int argc, char** argv)
{
    // The plug-in scanner runs this program again as its helper (decision 0047).
    installTestFormats();
    if (argc > 1 && std::strcmp (argv[1], kal::PluginManager::workerFlag) == 0)
        return kal::PluginManager::runWorker (argc, argv);
    // The crash reports' test runs this program again to crash it (on Windows, which can't fork: 0129).
    if (argc > 2 && std::strcmp (argv[1], "--kalibre-crash") == 0)
    {
        kal::crash::install (juce::File (juce::String::fromUTF8 (argv[2])), "test");
        kal::crashcontext::audioPlugin = "Riffer";
        volatile int* nothing = nullptr;
        *nothing = 1;   // a bad memory access
        return 0;
    }
   #if JUCE_WINDOWS
    installTestCrashPrinter();
   #endif
    juce::ScopedJuceInitialiser_GUI gui;
    int ran = 0;
    for (const auto& t : check::all())
    {
        if (argc > 1 && std::strstr (t.name, argv[1]) == nullptr) continue;
        check::current() = t.name;
        // Each test named as it starts, so a crash in CI's log says where it was.
        std::printf ("- %s\n", t.name);
        std::fflush (stdout);
        t.fn();
        ++ran;
    }
   #if JUCE_WINDOWS
    testsFinished();
   #endif
    if (check::failures() > 0)
    {
        std::printf ("%d failure(s) in %d tests\n", check::failures(), ran);
        return 1;
    }
    std::printf ("%d tests passed\n", ran);
    return 0;
}
