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

    A plug-in loaded into Kalibre may set handlers of its own for those
    signals (copy protection often does), and then Kalibre's never runs. So
    each start is noted (Running.txt) and each quit clears it: a start that
    finds it, and no report of Kalibre's since, knows the last run ended
    unexpectedly, and brings in macOS's own report of it (0140).
*/

#pragma once

#include <juce_core/juce_core.h>

#include <optional>

namespace kal::crash
{

void install (const juce::File& folder, const juce::String& version);
juce::File folder();     // Application Support/Kalibre/Crash Reports

// Reports written after `since`, newest first.
juce::Array<juce::File> since (const juce::File& folder, juce::Time since);
// Its first lines, in words: "a bad memory access, in Riffer".
juce::String summary (const juce::File& report);

// Kalibre is running: noted, so the next start knows if it ended unexpectedly.
// Returns when the last run started if it didn't end by quitting (else nullopt).
std::optional<juce::Time> markRunning (const juce::File& folder);
// Kalibre is quitting as asked.
void markQuit (const juce::File& folder);
// Where the system keeps its own crash reports: ~/Library/Logs/DiagnosticReports on the Mac.
juce::File systemReports();
// The system's newest report of a Kalibre crash since `since`, turned into one of
// Kalibre's in `folder` (what went wrong in Kalibre's words, the crashed thread's
// calls, and the system's file copied beside it); none: a missing File.
juce::File bringInSystemReport (const juce::File& folder, const juce::File& systemFolder, juce::Time since, const juce::String& version);

} // namespace kal::crash
