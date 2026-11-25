// Crash reports on Windows (decision 0129): what the Mac's signal handler
// writes (CrashReports.cpp, decision 0073), from an unhandled-exception
// filter, and on abort(). Only calls safe once things have gone wrong: no
// heap, Windows' own file calls; the stack as each frame's module and offset.
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "CrashReports.h"
#include "CrashContext.h"

#include <csignal>
#include <cstring>
#include <cwchar>
#include <ctime>

namespace kal::crash
{
namespace
{
wchar_t reportFolder[2048] {};   // wide, for a folder with any letters in it
char versionText[64] {};

void put (HANDLE h, const char* s)
{
    if (! s) return;
    DWORD written = 0;
    WriteFile (h, s, (DWORD) std::strlen (s), &written, nullptr);
}

void putNumber (HANDLE h, unsigned long long v, int base = 10)
{
    char buf[32];
    int i = 31;
    buf[i] = 0;
    do { const int d = (int) (v % (unsigned) base); buf[--i] = (char) (d < 10 ? '0' + d : 'A' + d - 10); v /= (unsigned) base; } while (v > 0 && i > 1);
    put (h, buf + i);
}

const char* describe (DWORD code)
{
    switch (code)
    {
        case EXCEPTION_ACCESS_VIOLATION:      return "a bad memory access";
        case EXCEPTION_ILLEGAL_INSTRUCTION:   return "an illegal instruction";
        case EXCEPTION_STACK_OVERFLOW:        return "it ran out of stack";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "a division by zero";
        case EXCEPTION_BREAKPOINT:            return "a trap";
        case 0xC0000409:                      return "a check failed and it stopped itself";
        case 0xE06D7363:                      return "an error nothing caught (a C++ exception)";
        case 0:                               return "an abort: something found it couldn't go on";
        default:                              return "an exception";
    }
}

volatile LONG writing = 0;

void write (DWORD code, CONTEXT*)
{
    if (InterlockedExchange (&writing, 1) != 0) return;   // once, whichever thread gets here first
    wchar_t path[2200];
    std::wcscpy (path, reportFolder);
    std::wcscat (path, L"\\Kalibre Crash ");
    wchar_t stamp[32];
    {
        long long t = (long long) ::time (nullptr);
        int i = 31;
        stamp[i] = 0;
        do { stamp[--i] = (wchar_t) (L'0' + t % 10); t /= 10; } while (t > 0 && i > 0);
        std::wcscat (path, stamp + i);
    }
    std::wcscat (path, L".txt");
    HANDLE h = CreateFileW (path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    put (h, "Kalibre ");
    put (h, versionText);
    put (h, " quit unexpectedly: ");
    put (h, describe (code));
    put (h, " (exception 0x");
    putNumber (h, code, 16);
    put (h, ").\r\n");
    if (const char* plugin = crashcontext::audioPlugin)
    {
        put (h, "In the plug-in: ");
        put (h, plugin);
        put (h, " (it was making sound when it happened).\r\n");
    }
    put (h, "Time: ");
    putNumber (h, (unsigned long long) ::time (nullptr));
    put (h, " seconds since 1970.\r\n\r\nWhere, the newest call first (each as its file + an offset):\r\n");
    void* frames[64];
    const USHORT n = CaptureStackBackTrace (0, 64, frames, nullptr);
    for (USHORT i = 0; i < n; ++i)
    {
        HMODULE module = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR) frames[i], &module))
            GetModuleFileNameA (module, name, MAX_PATH);
        const char* file = std::strrchr (name, '\\');
        put (h, file ? file + 1 : name);
        put (h, " + 0x");
        putNumber (h, (unsigned long long) ((const char*) frames[i] - (const char*) module), 16);
        put (h, "\r\n");
    }
    CloseHandle (h);
}

LONG WINAPI filter (EXCEPTION_POINTERS* e)
{
    write (e && e->ExceptionRecord ? e->ExceptionRecord->ExceptionCode : 1, e ? e->ContextRecord : nullptr);
    return EXCEPTION_CONTINUE_SEARCH;   // on as before: Windows' own report, and the end of the program
}

void onAbort (int)
{
    write (0, nullptr);
    std::signal (SIGABRT, SIG_DFL);
    std::abort();
}
} // namespace

void install (const juce::File& f, const juce::String& version)
{
    f.createDirectory();
    f.getFullPathName().copyToUTF16 ((juce::CharPointer_UTF16::CharType*) reportFolder, (int) sizeof (reportFolder) - 2);
    version.copyToUTF8 (versionText, sizeof (versionText) - 1);
    SetUnhandledExceptionFilter (filter);
    std::signal (SIGABRT, onAbort);
    // A stack overflow leaves too little to write with: keep some back for the filter.
    ULONG keep = 64 * 1024;
    SetThreadStackGuarantee (&keep);
}

} // namespace kal::crash
