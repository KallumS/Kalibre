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

#include <atomic>
#include <csignal>
#include <thread>
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

// The main thread's stack, from another thread: where a test that hangs is stuck.
void printThreadStack (HANDLE thread)
{
    HANDLE process = GetCurrentProcess();
    SymSetOptions (SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize (process, nullptr, TRUE);
    SuspendThread (thread);
    CONTEXT ctx {};
    ctx.ContextFlags = CONTEXT_FULL;
    GetThreadContext (thread, &ctx);
    STACKFRAME64 frame {};
#if defined (_M_X64)
    const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrStack.Offset = ctx.Rsp;
#else
    const DWORD machine = IMAGE_FILE_MACHINE_ARM64;
    frame.AddrPC.Offset = ctx.Pc;
    frame.AddrFrame.Offset = ctx.Fp;
    frame.AddrStack.Offset = ctx.Sp;
#endif
    frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;
    alignas (SYMBOL_INFO) char buffer[sizeof (SYMBOL_INFO) + 512];
    for (int i = 0; i < 60 && StackWalk64 (machine, process, thread, &frame, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr); ++i)
    {
        auto* symbol = reinterpret_cast<SYMBOL_INFO*> (buffer);
        symbol->SizeOfStruct = sizeof (SYMBOL_INFO);
        symbol->MaxNameLen = 511;
        DWORD64 offset = 0;
        const bool named = SymFromAddr (process, frame.AddrPC.Offset, &offset, symbol);
        IMAGEHLP_LINE64 line {};
        line.SizeOfStruct = sizeof line;
        DWORD column = 0;
        const bool lined = SymGetLineFromAddr64 (process, frame.AddrPC.Offset, &column, &line);
        std::printf ("  %2d  %s", i, named ? symbol->Name : "?");
        if (lined) std::printf ("  (%s:%lu)", line.FileName, line.LineNumber);
        std::printf ("\n");
    }
    std::fflush (stdout);
}

std::atomic<unsigned long long> testStartedAt { 0 };
const char* testName = "";
HANDLE mainThread = nullptr;
} // namespace

// A test that runs past `seconds`: its name and where the main thread is, then the end.
void testStarting (const char* name)
{
    testName = name;
    testStartedAt = GetTickCount64();
    if (mainThread != nullptr) return;
    DuplicateHandle (GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &mainThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
    std::thread ([]
    {
        for (;;)
        {
            Sleep (5000);
            if (finished) return;
            if (GetTickCount64() - testStartedAt.load() > 180000)
            {
                std::printf ("\n===== The test hung for 3 minutes: %s =====\nThe main thread is at:\n", testName);
                printThreadStack (mainThread);
                std::_Exit (4);
            }
        }
    }).detach();
}

void installTestCrashPrinter()
{
    SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter (onCrash);
    std::signal (SIGABRT, onAbort);
    std::set_terminate (onTerminate);
    std::atexit (onExit);
}

void testsFinished() { finished = true; }
