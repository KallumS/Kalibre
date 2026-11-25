#include "HelperProcess.h"

#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>

namespace kal::helper
{
namespace
{
std::wstring wide (const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar (CP_UTF8, 0, s.data(), (int) s.size(), nullptr, 0);
    std::wstring w ((size_t) n, L'\0');
    MultiByteToWideChar (CP_UTF8, 0, s.data(), (int) s.size(), w.data(), n);
    return w;
}

// Quoted as CommandLineToArgvW reads it back: backslashes doubled before a quote.
void appendQuoted (std::wstring& line, const std::wstring& arg)
{
    line += L'"';
    size_t slashes = 0;
    for (auto c : arg)
    {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') line.append (slashes * 2 + 1, L'\\');
        else line.append (slashes, L'\\');
        slashes = 0;
        line += c;
    }
    line.append (slashes * 2, L'\\');
    line += L'"';
}
} // namespace

bool isCrash (unsigned long code) { return (code & 0xF0000000ul) == 0xC0000000ul || code == 0x80000003ul; }

std::string crashName (unsigned long code)
{
    char hex[16];
    std::snprintf (hex, sizeof hex, "0x%08lX", code);
    switch (code)
    {
        case 0xC0000005ul: return std::string ("a bad memory access (") + hex + ")";
        case 0xC000001Dul: return std::string ("an illegal instruction (") + hex + ")";
        case 0xC00000FDul: return std::string ("it ran out of stack (") + hex + ")";
        case 0xC0000409ul: return std::string ("it stopped itself: a check failed (") + hex + ")";
        case 0xC0000374ul: return std::string ("its memory was corrupted (") + hex + ")";
        case 0x80000003ul: return std::string ("a breakpoint (") + hex + ")";
        default:           return std::string ("exception ") + hex;
    }
}

Result run (const std::string& exe, const std::vector<std::string>& args, const std::string& logFile,
            int timeoutMs, const std::function<bool()>& shouldStop)
{
    std::wstring line;
    appendQuoted (line, wide (exe));
    for (auto& a : args) { line += L' '; appendQuoted (line, wide (a)); }

    SECURITY_ATTRIBUTES inherit { sizeof (SECURITY_ATTRIBUTES), nullptr, TRUE };
    HANDLE nul = CreateFileW (L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);
    HANDLE log = CreateFileW (wide (logFile).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_ALWAYS, 0, nullptr);

    STARTUPINFOW si {};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nullptr;
    si.hStdOutput = nul;
    si.hStdError = log != INVALID_HANDLE_VALUE ? log : nul;
    PROCESS_INFORMATION pi {};
    // No window of its own, and no "this program has stopped working" box when a plug-in crashes it.
    const UINT oldMode = SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    const BOOL started = CreateProcessW (nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    SetErrorMode (oldMode);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle (nul);
    if (log != INVALID_HANDLE_VALUE) CloseHandle (log);
    if (! started) return { Result::CouldNotStart, GetLastError() };
    CloseHandle (pi.hThread);

    Result r;
    const DWORD startedAt = GetTickCount();
    for (;;)
    {
        if (WaitForSingleObject (pi.hProcess, 15) == WAIT_OBJECT_0)
        {
            DWORD code = 0;
            GetExitCodeProcess (pi.hProcess, &code);
            r = { isCrash (code) ? Result::Crashed : Result::Exited, code };
            break;
        }
        const bool stop = shouldStop && shouldStop();
        if (stop || (int) (GetTickCount() - startedAt) > timeoutMs)
        {
            TerminateProcess (pi.hProcess, 1);
            WaitForSingleObject (pi.hProcess, 5000);
            r = { stop ? Result::Cancelled : Result::TimedOut, 0 };
            break;
        }
    }
    CloseHandle (pi.hProcess);
    return r;
}

} // namespace kal::helper
