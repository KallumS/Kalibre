// On Windows CI (decision 0129): when the tests end early, say how and where,
// with function names: a crash (its exception code), an abort, an error no
// one caught, or an exit before the last test. The tests' own logic is the
// same on every system; only this report is Windows'.
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>

namespace
{
bool finished = false;

void printStack (const char* why)
{
    std::printf ("\n===== The tests ended early: %s =====\n", why);
    HANDLE process = GetCurrentProcess();
    SymSetOptions (SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize (process, nullptr, TRUE);
    void* frames[62];
    const USHORT n = CaptureStackBackTrace (0, 62, frames, nullptr);
    alignas (SYMBOL_INFO) char buffer[sizeof (SYMBOL_INFO) + 512];
    for (USHORT i = 0; i < n; ++i)
    {
        auto* symbol = reinterpret_cast<SYMBOL_INFO*> (buffer);
        symbol->SizeOfStruct = sizeof (SYMBOL_INFO);
        symbol->MaxNameLen = 511;
        DWORD64 offset = 0;
        const auto address = (DWORD64) frames[i];
        const bool named = SymFromAddr (process, address, &offset, symbol);
        IMAGEHLP_LINE64 line {};
        line.SizeOfStruct = sizeof line;
        DWORD column = 0;
        const bool lined = SymGetLineFromAddr64 (process, address, &column, &line);
        std::printf ("  %2u  %s", (unsigned) i, named ? symbol->Name : "?");
        if (lined) std::printf ("  (%s:%lu)", line.FileName, line.LineNumber);
        std::printf ("\n");
    }
    std::fflush (stdout);
}

LONG WINAPI onCrash (EXCEPTION_POINTERS* e)
{
    char why[64];
    std::snprintf (why, sizeof why, "a crash, exception 0x%08lX", e && e->ExceptionRecord ? e->ExceptionRecord->ExceptionCode : 0ul);
    printStack (why);
    return EXCEPTION_EXECUTE_HANDLER;   // ended here, with no dialog waiting for a click
}

void onAbort (int) { printStack ("abort()"); std::_Exit (3); }

void onTerminate()
{
    const char* what = "an error no one caught";
    try { if (auto e = std::current_exception()) std::rethrow_exception (e); }
    catch (const std::exception& x) { std::printf ("\n  (it said: %s)\n", x.what()); }
    catch (...) {}
    printStack (what);
    std::_Exit (3);
}

void onExit() { if (! finished) printStack ("exit() before the last test"); }
} // namespace

void installTestCrashPrinter()
{
    SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter (onCrash);
    std::signal (SIGABRT, onAbort);
    std::set_terminate (onTerminate);
    std::atexit (onExit);
}

void testsFinished() { finished = true; }
