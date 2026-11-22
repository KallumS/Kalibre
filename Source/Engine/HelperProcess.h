/*
    HelperProcess - running the plug-in scanner's helper on Windows (decision
    0129), as PluginManager runs it on the Mac with posix_spawn: no shell, its
    output thrown away, what it says on stderr appended to a log, waited for
    with a time limit, and killed when that passes or the scan is cancelled.

    Apart from PluginManager so Windows' headers never meet JUCE's names.
*/

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace kal::helper
{

struct Result
{
    enum How { Exited, Crashed, TimedOut, Cancelled, CouldNotStart } how = CouldNotStart;
    unsigned long code = 0;   // its exit code; for a crash, the exception's (0xC0000005: a bad memory access)
};

// Paths and arguments in UTF-8.
Result run (const std::string& exe, const std::vector<std::string>& args, const std::string& logFile,
            int timeoutMs, const std::function<bool()>& shouldStop);

// A crash's code in words: "a bad memory access (0xC0000005)".
std::string crashName (unsigned long code);
// Is this exit code a crash (an exception that ended it) rather than a return?
bool isCrash (unsigned long code);

} // namespace kal::helper
