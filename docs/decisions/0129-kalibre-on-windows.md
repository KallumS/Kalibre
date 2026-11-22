# 0129. Kalibre on Windows: Microsoft's compiler, Windows' words and keys

## Context

Asked (22 November): a new branch, **Windows**, with Kalibre ported to
Windows; then, while it was being done: the words on screen and the key
commands adapted to Windows too. Kalibre was a Mac app (0001), built and
tested on Linux too, so all but a few corners were already JUCE's and ran
anywhere JUCE does.

## Decision

- **Built on CI** (`.github/workflows/build.yml`, job *Windows app*, on
  windows-2022 with Visual Studio 2022, x64): Kalibre, the core tests and the
  app tests; **Kalibre-Windows.zip** from the run's Artifacts (Kalibre.exe,
  its Scripts folder, *Read me first.txt*), attached to releases with the
  Mac's `.dmg`. Nothing to install: the C++ runtime is inside Kalibre.exe.
  MSVC is given `/utf-8 /bigobj /MP`, NOMINMAX and `_USE_MATH_DEFINES`.
  JUCE 9 won't build with MinGW.
- **What was Unix's alone**, done Windows' way:
  - the plug-in scanner's **helper** (0047), run with `CreateProcessW`
    (`Engine/HelperProcessWin.cpp`): no window, no shell, stderr to the
    log, killed on its time limit or when the scan stops; a crash is told
    by its exception code (0xC0000005: a bad memory access), and Windows'
    own crash box is kept away;
  - **crash reports** (0073) from an unhandled-exception filter and abort()
    (`App/CrashReportsWin.cpp`): the same report, the stack as each frame's
    file and offset; its test runs the test program again to crash it, as
    Windows can't fork;
  - a **CLAP plug-in's window**, a child window held by JUCE's HWNDComponent
    (`Engine/ClapGuiWin.cpp`); CLAP found in Common Files\CLAP and
    %LOCALAPPDATA%\Programs\Common\CLAP, CLAP_PATH parted at ";";
  - the workers' spin-wait (`_mm_pause`) and a count of bits (`std::popcount`);
  - **paths**: Core parts folders at `\` as at `/` (the Sounds tab's
    folders, drum sounds' folders); Final Cut XML writes and reads
    `file:///C:/...`.
- **Windows' words** (`Core/Platform.h`, `ui::words()` in Theme.h): Option
  is **Alt**, Command **Ctrl**, joined with a plus (Alt+click, Ctrl+D) as
  Microsoft writes them; Return **Enter**; the Finder **File Explorer**
  (Show in File Explorer); the Trash **the Recycle Bin**; this Mac **this
  PC**; Terminal **PowerShell**. Every tip, menu item and message that named
  one says the other on Windows; the toolbox's tips too (from Core).
- **Windows' menus**: the Mac's Kalibre menu is **File > Settings** (Audio
  and MIDI Settings, Plug-in Manager, Library Folders), and every message
  that pointed there says so; Quit is **File > Exit** (Alt+F4).
- **Windows' keys**: JUCE's Command is Ctrl on Windows, so every key works
  as on the Mac with Ctrl for ⌘. Shown in words: Ctrl+Shift+S, Enter,
  Backspace, Delete. Redo is also on **Ctrl+Y**, Windows' own.

## Consequences

- Not on Windows yet: the Video tab's picture (AVFoundation), ARA
  (Melodyne), the Mac's network MIDI. AU is the Mac's alone.
- A plug-in's window in the rack is a child window that JUCE can't clip, so
  in a short zone it isn't fitted as on the Mac (0058): roomier zone, or its
  own window.
- Kalibre.exe isn't signed: the first start asks (More info > Run anyway).
- Tried only by CI: built, its tests run. Not yet by the musician, nor with
  real plug-ins, audio interfaces or MIDI gear on Windows.
- Tested: the words for each system; Core's paths with backslashes; Final
  Cut XML's Windows links; on Windows CI, every core and app test,
  including a crash report written by the Windows handler.
