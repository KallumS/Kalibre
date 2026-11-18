/*
    CrashReports - when Kalibre crashes, a report of where (decision 0073).

    install() sets handlers for the signals a crash raises (a bad memory
    access, an abort, an illegal instruction). The handler writes, with
    nothing but calls safe inside a signal handler, a text file into the
    folder: what went wrong, the plug-in the audio thread was in if it was
    in one, and the stack. Then the signal goes on as before, so the Mac
    writes its own report too and the program ends.

    On the next start, unread() finds reports newer than the last one shown,
    and the message line says what happened (Help > Show Crash Reports).
*/

#pragma once

#include <juce_core/juce_core.h>

namespace kal::crash
{

void install (const juce::File& folder, const juce::String& version);
juce::File folder();     // Application Support/Kalibre/Crash Reports

// Reports written after `since`, newest first.
juce::Array<juce::File> since (const juce::File& folder, juce::Time since);
// Its first lines, in words: "a bad memory access, in Riffer".
juce::String summary (const juce::File& report);

} // namespace kal::crash
