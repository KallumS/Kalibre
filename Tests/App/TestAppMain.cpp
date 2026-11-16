// The window's and the engine's tests: JUCE is up, no audio device is needed.
#include "Check.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginManager.h"

#include <cstring>

void installTestFormats();   // TestPlugins.cpp

int main (int argc, char** argv)
{
    // The plug-in scanner runs this program again as its helper (decision 0047).
    installTestFormats();
    if (argc > 1 && std::strcmp (argv[1], kal::PluginManager::workerFlag) == 0)
        return kal::PluginManager::runWorker (argc, argv);
    juce::ScopedJuceInitialiser_GUI gui;
    int ran = 0;
    for (const auto& t : check::all())
    {
        if (argc > 1 && std::strstr (t.name, argv[1]) == nullptr) continue;
        check::current() = t.name;
        t.fn();
        ++ran;
    }
    if (check::failures() > 0)
    {
        std::printf ("%d failure(s) in %d tests\n", check::failures(), ran);
        return 1;
    }
    std::printf ("%d tests passed\n", ran);
    return 0;
}
