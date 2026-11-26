/*
    Platform - the words the Mac, Windows and Linux each use (decisions 0129, 0139).

    On screen, a key or a place is named as the system it runs on names it:
    Option-click on the Mac is Alt+click on Windows, Command is Ctrl, Return
    is Enter, the Finder is File Explorer, the Trash the Recycle Bin, "this
    Mac" this PC. Keys themselves need nothing: JUCE's Command is Ctrl on
    Windows. Apple writes a key and a click joined by a hyphen, Microsoft
    with a plus. Linux writes keys as Windows does (Ctrl+D, Alt+click); its
    file manager has many names (Files, Dolphin, Thunar), so it's "the file
    manager".

    No JUCE in here: the core's own tips (the toolbox's) use it too.
*/

#pragma once

#include <string>

namespace kal::platform
{

#if defined (_WIN32)
inline constexpr bool windows = true;
#else
inline constexpr bool windows = false;
#endif
#if defined (__linux__)
inline constexpr bool linuxOs = true;
#else
inline constexpr bool linuxOs = false;
#endif
inline constexpr bool macOs = ! windows && ! linuxOs;

struct Words
{
    const char* option;          // the key beside the space bar that changes a click: "Option" / "Alt"
    const char* command;         // the shortcut key: "Command" / "Ctrl"
    const char* control;         // "Control" / "Ctrl"
    const char* join;            // between a key and a click or a key: "-" / "+"
    const char* returnKey;       // "Return" / "Enter"
    const char* fileManager;     // "the Finder" / "File Explorer"
    const char* showInFiles;     // "Show in Finder" / "Show in File Explorer"
    const char* trash;           // "the Trash" / "the Recycle Bin"
    const char* computer;        // "Mac" / "PC": "on this Mac", "on this PC"
    const char* terminal;        // where a command is pasted
    const char* settings;        // where the Plug-in Manager and Library Folders are: "Kalibre" / "File > Settings"
    const char* settingsMenu;    // the same in a sentence: "the Kalibre menu" / "File > Settings"
};

inline constexpr Words mac { "Option", "Command", "Control", "-", "Return", "the Finder", "Show in Finder", "the Trash", "Mac", "Terminal", "Kalibre", "the Kalibre menu" };
inline constexpr Words pc  { "Alt", "Ctrl", "Ctrl", "+", "Enter", "File Explorer", "Show in File Explorer", "the Recycle Bin", "PC", "PowerShell", "File > Settings", "File > Settings" };

inline constexpr Words linuxWords { "Alt", "Ctrl", "Ctrl", "+", "Enter", "the file manager", "Show in File Manager", "the Trash", "computer", "Terminal", "File > Settings", "File > Settings" };

// Another system's words instead of this one's: the renderer draws the user
// guide's pictures in the Mac's words, wherever it runs.
inline const Words*& wordsInstead() { static const Words* w = nullptr; return w; }
inline const Words& words()
{
    if (wordsInstead() != nullptr) return *wordsInstead();
    return windows ? pc : linuxOs ? linuxWords : mac;
}

// "Option-click" / "Alt+click", "Command-D" / "Ctrl+D": a key and what's done with it, as each system writes it.
inline std::string with (const char* key, const std::string& what, const Words& w = words()) { return std::string (key) + w.join + what; }
inline std::string optionClick (const std::string& what = "click", const Words& w = words()) { return with (w.option, what, w); }
inline std::string commandKey (const std::string& what, const Words& w = words()) { return with (w.command, what, w); }
// "Kalibre > Plug-in Manager" / "File > Settings > Plug-in Manager".
inline std::string inSettings (const std::string& item, const Words& w = words()) { return std::string (w.settings) + " > " + item; }

} // namespace kal::platform
