// Windows starts a windowed program at WinMain, not main (decision 0129).
// Kalibre's arguments (the scanner helper's plug-in paths among them) are
// read as Windows gives them, in UTF-16, and handed on in UTF-8.
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <string>
#include <vector>

int kalibreMain (int argc, char* argv[]);   // Main.cpp

int __stdcall WinMain (HINSTANCE, HINSTANCE, LPSTR, int)
{
    int n = 0;
    LPWSTR* wide = CommandLineToArgvW (GetCommandLineW(), &n);
    std::vector<std::string> args;
    for (int i = 0; i < n; ++i)
    {
        const int size = WideCharToMultiByte (CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s ((size_t) (size > 0 ? size - 1 : 0), '\0');
        if (size > 1) WideCharToMultiByte (CP_UTF8, 0, wide[i], -1, s.data(), size, nullptr, nullptr);
        args.push_back (std::move (s));
    }
    LocalFree (wide);
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back (a.data());
    argv.push_back (nullptr);
    return kalibreMain ((int) args.size(), argv.data());
}
