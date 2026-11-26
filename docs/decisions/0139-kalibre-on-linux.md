# 0139. Kalibre on Linux

## Context

Asked (26 November), after the plug-in pictures: port Kalibre to Linux too.
Kalibre was already built and tested on Linux by CI (its tests and the user
guide's pictures run in a Linux container, 0023), so it compiled and ran
there; what it lacked was what a musician on Linux would need: a download,
a place in the applications menu, Linux's plug-in formats and audio, its
own words and keys, and plug-in windows that stay inside their panels.

## Decision

- **One branch, three systems** (0137): what's Linux's alone is behind
  `JUCE_LINUX` (CMake: the `else()` after APPLE and WIN32), or
  `platform::linuxOs` in the core.
- **Built on CI** (`.github/workflows/build.yml`, job *Linux app*): on
  Ubuntu 22.04 with GCC 12, the oldest Ubuntu still supported, so the
  program runs on Linuxes as old (glibc 2.35: Ubuntu 22.04, Debian 12,
  Fedora 37 and later). Its core and app tests run there too.
  **Kalibre-Linux.tar.gz** from the run's Artifacts, attached to releases
  with the Mac's and Windows': Kalibre (the program, stripped), Scripts,
  *Read me first.txt*, and `install.sh`, which puts a `.desktop` entry, the
  icon and the `.kalibre` file type in the home folder (nothing outside it,
  no password), and `~/.local/bin/kalibre`; `--remove` takes them out.
  x86-64 only.
- **Plug-ins**: VST3 and CLAP as before (CLAP's window through XEmbed,
  already), and now **LV2** and **LADSPA** (JUCE's hosts, Linux only:
  `JUCE_PLUGINHOST_LV2`, `JUCE_PLUGINHOST_LADSPA`). LADSPA has no windows:
  Kalibre's generic panel shows its controls. ARA isn't built: Melodyne
  isn't made for Linux.
- **Audio**: **JACK** (`JUCE_JACK`; JUCE loads libjack when it's there, so
  Kalibre starts without it) besides ALSA; PipeWire's JACK works as JACK.
- **Plug-in windows in the rack** (`App/PluginClipLinux.cpp`): an X11
  window embedded in Kalibre's, which X draws over whatever Kalibre draws,
  hung out of its panel. Each embedded window inside a panel (every
  `XEmbedComponent` in it: VST3's, LV2's, CLAP's) is given a shape with X's
  SHAPE extension, so only what's inside the panel shows, checked as it's
  placed and ten times a second, as Windows' regions are (0129). An X
  window can't be scaled, so a plug-in's window is shown at its own size,
  the part that fits. Asking a VST3 to draw itself smaller, as on Windows,
  was tried: a JUCE-made VST3 answered with a smaller window, Kalibre asked
  again, and it shrank towards nothing until X refused a window of no size.
- **X's errors never end Kalibre** (`keepXErrorsQuiet`): X's own handler
  ends the program on any error, such as a plug-in's window gone a moment
  before Kalibre shapes it. Kalibre's handler notes it and carries on; it's
  set as Kalibre starts and again before Kalibre's own X calls, as plug-ins
  built with JUCE set and restore handlers of their own.
- **Linux's words** (`Core/Platform.h`): keys as Windows writes them (Ctrl,
  Alt, joined with a plus; Enter); **Show in File Manager** (Linux's has
  many names); the Trash; "this computer"; Terminal; **File > Settings** for
  Audio and MIDI Settings, Plug-in Manager and Library Folders; **Help >
  About Kalibre**; **File > Quit** on **Ctrl+Q** (bound, as Linux programs
  have it); Redo on Ctrl+Y too. The renderer draws the user guide's pictures
  in the Mac's words wherever it runs (`platform::wordsInstead`), so they
  stay the musician's.
- **Plug-in pictures** (0138) read from X11 (`PluginPicturesLinux.cpp`).
- Settings, the plug-in list, pictures, scripts and crash reports in
  `~/.config/Kalibre` (JUCE's place for them on Linux).

## Consequences

- No Wayland of its own: Kalibre runs through XWayland, as JUCE does, and
  plug-ins' X11 windows need it anyway.
- The video's picture isn't shown on Linux (JUCE has no video there), and a
  film's AAC sound can't be read.
- Network MIDI's session runs but isn't announced (no DNS-SD): a Mac adds it
  by address.
- A plug-in window taller than a short zone is cut, not shrunk: its own
  window (⧉) shows it all.
- Tested: Linux's words; on Linux CI every core and app test. Tried here
  with a VST3 and an LV2 built from JUCE for the purpose (a 700 by 420
  window) and the LADSPA SDK's examples: all scanned and filed; the VST3
  and LV2 opened side by side in a 260-pixel rack, each cut to its panel
  above the scroll bar; the camera button took each one's picture, the
  whole window. Not yet by the musician, nor with commercial plug-ins,
  audio interfaces or MIDI gear on Linux.
