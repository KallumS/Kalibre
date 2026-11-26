#include "CrashReports.h"

#include "CrashContext.h"

#include <csignal>
#include <cstring>
#include <ctime>
#if ! JUCE_WINDOWS   // Windows' handler is in CrashReportsWin.cpp (decision 0129)
 #include <execinfo.h>
 #include <fcntl.h>
 #include <unistd.h>
#endif

namespace kal::crash
{
using namespace juce;

#if ! JUCE_WINDOWS
namespace
{
char reportFolder[2048] {};
char versionText[64] {};
stack_t altStack {};

// Only calls a signal handler may make: write, open, time, backtrace_symbols_fd.
void put (int fd, const char* s) { if (s) { const auto n = std::strlen (s); auto ignored = ::write (fd, s, n); (void) ignored; } }

void putNumber (int fd, long long v)
{
    char buf[32];
    int i = 31;
    buf[i] = 0;
    const bool negative = v < 0;
    unsigned long long u = negative ? (unsigned long long) (-v) : (unsigned long long) v;
    do { buf[--i] = (char) ('0' + u % 10); u /= 10; } while (u > 0 && i > 1);
    if (negative) buf[--i] = '-';
    put (fd, buf + i);
}

const char* describe (int sig)
{
    switch (sig)
    {
        case SIGSEGV: return "a bad memory access (signal 11)";
        case SIGBUS:  return "a bad memory access (signal 10)";
        case SIGABRT: return "an abort: something found it couldn't go on (signal 6)";
        case SIGILL:  return "an illegal instruction (signal 4)";
        case SIGFPE:  return "a bad arithmetic operation (signal 8)";
        case SIGTRAP: return "a trap (signal 5)";
        default:      return "a signal";
    }
}

void handler (int sig, siginfo_t*, void*)
{
    char path[2200];
    std::strcpy (path, reportFolder);
    std::strcat (path, "/Kalibre Crash ");
    char stamp[32];
    {
        long long t = (long long) ::time (nullptr);
        int i = 31;
        stamp[i] = 0;
        do { stamp[--i] = (char) ('0' + t % 10); t /= 10; } while (t > 0 && i > 0);
        std::strcat (path, stamp + i);
    }
    std::strcat (path, ".txt");
    const int fd = ::open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        put (fd, "Kalibre ");
        put (fd, versionText);
        put (fd, " quit unexpectedly: ");
        put (fd, describe (sig));
        put (fd, ".\n");
        if (const char* plugin = crashcontext::audioPlugin)
        {
            put (fd, "In the plug-in: ");
            put (fd, plugin);
            put (fd, " (it was making sound when it happened).\n");
        }
        put (fd, "Time: ");
        putNumber (fd, (long long) ::time (nullptr));
        put (fd, " seconds since 1970.\n\nWhere, the newest call first:\n");
        void* frames[128];
        const int n = ::backtrace (frames, 128);
        ::backtrace_symbols_fd (frames, n, fd);
        ::close (fd);
    }
    // On as before: the Mac's own report, and the end of the program.
    ::signal (sig, SIG_DFL);
    ::raise (sig);
}
} // namespace

#endif

File folder()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
          #if JUCE_MAC
           .getChildFile ("Application Support")
          #endif
           .getChildFile ("Kalibre").getChildFile ("Crash Reports");
}

#if ! JUCE_WINDOWS
void install (const File& f, const String& version)
{
    f.createDirectory();
    f.getFullPathName().copyToUTF8 (reportFolder, sizeof (reportFolder) - 1);
    version.copyToUTF8 (versionText, sizeof (versionText) - 1);
    // A stack of its own, so a crash from running out of stack can still be written down.
    static char stack[64 * 1024];
    altStack.ss_sp = stack;
    altStack.ss_size = sizeof (stack);
    altStack.ss_flags = 0;
    ::sigaltstack (&altStack, nullptr);
    struct sigaction sa {};
    sa.sa_sigaction = handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset (&sa.sa_mask);
    for (int sig : { SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE, SIGTRAP })
        ::sigaction (sig, &sa, nullptr);
}

#endif

Array<File> since (const File& f, Time t)
{
    Array<File> out;
    for (auto& r : f.findChildFiles (File::findFiles, false, "Kalibre Crash *.txt"))
        if (r.getLastModificationTime() > t) out.add (r);
    std::sort (out.begin(), out.end(), [] (const File& a, const File& b) { return a.getLastModificationTime() > b.getLastModificationTime(); });
    return out;
}

String summary (const File& report)
{
    StringArray lines;
    report.readLines (lines);
    if (lines.isEmpty()) return {};
    String what = lines[0].fromFirstOccurrenceOf ("quit unexpectedly: ", false, false).upToLastOccurrenceOf (" (signal", false, false).upToLastOccurrenceOf (" (exception", false, false);
    if (what.isEmpty()) what = lines[0];
    what = what.trimCharactersAtEnd (".");
    if (lines.size() > 1 && lines[1].startsWith ("In the plug-in: "))
        what << ", in " << lines[1].fromFirstOccurrenceOf ("In the plug-in: ", false, false).upToFirstOccurrenceOf (" (", false, false);
    return what;
}

//==============================================================================
namespace
{
File runningFile (const File& folder) { return folder.getChildFile ("Running.txt"); }

// macOS's names for what went wrong, in Kalibre's words.
String inWords (const String& signal, const String& type)
{
    if (signal == "SIGSEGV" || signal == "SIGBUS" || type == "EXC_BAD_ACCESS") return "a bad memory access";
    if (signal == "SIGABRT") return "an abort: something found it couldn't go on";
    if (signal == "SIGILL" || type == "EXC_BAD_INSTRUCTION") return "an illegal instruction";
    if (signal == "SIGFPE" || type == "EXC_ARITHMETIC") return "a bad arithmetic operation";
    if (signal == "SIGTRAP" || type == "EXC_BREAKPOINT") return "a trap";
    if (signal == "SIGKILL") return "being stopped by macOS";
    return signal.isNotEmpty() ? signal : type.isNotEmpty() ? type : String ("an unknown cause");
}
} // namespace

std::optional<Time> markRunning (const File& folder)
{
    folder.createDirectory();
    const auto f = runningFile (folder);
    std::optional<Time> unexpected;
    if (f.existsAsFile())
        unexpected = Time (f.loadFileAsString().trim().getLargeIntValue());
    f.replaceWithText (String (Time::currentTimeMillis()));
    return unexpected;
}

void markQuit (const File& folder) { runningFile (folder).deleteFile(); }

File systemReports()
{
   #if JUCE_MAC
    return File::getSpecialLocation (File::userHomeDirectory).getChildFile ("Library/Logs/DiagnosticReports");
   #else
    return {};
   #endif
}

File bringInSystemReport (const File& folder, const File& systemFolder, Time since, const String& version)
{
    if (systemFolder == File() || ! systemFolder.isDirectory()) return {};
    // The newest of Kalibre's since then: "Kalibre-2026-11-26-111219.ips".
    File newest;
    for (auto& f : systemFolder.findChildFiles (File::findFiles, false, "Kalibre*.ips"))
        if (f.getLastModificationTime() > since && (newest == File() || f.getLastModificationTime() > newest.getLastModificationTime()))
            newest = f;
    if (newest == File()) return {};

    // An .ips is a line of JSON about the report, then the report as JSON.
    const auto text = newest.loadFileAsString();
    const auto header = JSON::parse (text.upToFirstOccurrenceOf ("\n", false, false));
    const auto body = JSON::parse (text.fromFirstOccurrenceOf ("\n", false, false));
    const auto ran = header.getProperty ("app_version", version).toString();
    String what = "an unknown cause", where;
    if (auto* o = body.getDynamicObject())
    {
        const auto exception = o->getProperty ("exception");
        what = inWords (exception.getProperty ("signal", {}).toString(), exception.getProperty ("type", {}).toString());
        // The crashed thread's first calls, each with the program or plug-in it's in.
        const auto images = o->getProperty ("usedImages");
        const int faulting = (int) o->getProperty ("faultingThread");
        const auto threads = o->getProperty ("threads");
        if (threads.isArray() && faulting >= 0 && faulting < threads.size())
        {
            const auto frames = threads[faulting].getProperty ("frames", {});
            for (int i = 0; frames.isArray() && i < jmin (12, frames.size()); ++i)
            {
                const auto frame = frames[i];
                const int image = (int) frame.getProperty ("imageIndex", -1);
                const auto in = images.isArray() && image >= 0 && image < images.size() ? images[image].getProperty ("name", {}).toString() : String();
                where << "  " << i << "  " << frame.getProperty ("symbol", "?").toString() << (in.isNotEmpty() ? "  (" + in + ")" : String()) << "\n";
            }
        }
    }
    folder.createDirectory();
    const auto copy = folder.getChildFile (newest.getFileName());
    newest.copyFileTo (copy);
    const auto report = folder.getChildFile ("Kalibre Crash " + String (newest.getLastModificationTime().toMilliseconds() / 1000) + ".txt");
    report.replaceWithText ("Kalibre " + ran + " quit unexpectedly: " + what + ".\n"
                            "Kalibre's own report wasn't written (a plug-in may have put its own crash handler in Kalibre's place); "
                            "this is macOS's, " + newest.getFileName() + ", beside this file.\n\n"
                            "Where, the newest call first:\n" + where);
    return report;
}

} // namespace kal::crash
