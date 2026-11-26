# Kalibre

A DAW for Apple silicon Macs, Windows PCs and Linux (JUCE 9, C++20),
everything in one window. One branch, `main`, holds all three (0137, 0139):
what's only one system's is behind `JUCE_MAC`, `JUCE_WINDOWS` or
`JUCE_LINUX`, and CI builds and tests each.
Built for one musician, who is not a developer: explain in plain words, show
renders of what changed, and make sure every change reaches them as the CI's
`.dmg` (Actions > Build > Kalibre-macOS), on Windows its `.zip`
(Kalibre-Windows), and on Linux its `.tar.gz` (Kalibre-Linux).

## Where the reasons are

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md): the shape, how sound flows, rules easy to break. Start here.
- [`docs/decisions/`](docs/decisions/README.md): one record per choice, by number: **(0009)** is the piano roll's mouse rules.
- [`docs/FEATURES.md`](docs/FEATURES.md): what's done, what's partly done, what isn't. Keep it true.
- [`docs/USER-GUIDE.md`](docs/USER-GUIDE.md): for the musician: every function, menu, key and click, with pictures (`docs/guide/`, from KalibreRender). Change it when what they see or press changes.
- [`docs/DEVICES.md`](docs/DEVICES.md): every built-in device in detail, from the family's own READMEs (clone them from KallumS), with what differs in Kalibre.
- [`docs/sessions/`](docs/sessions/): one log per working session.
- [`docs/uat/`](docs/uat/): user acceptance tests, songs made in the real app, with its pictures (0109).
- [`docs/UPDATING-ENGINES.md`](docs/UPDATING-ENGINES.md): bringing the family's engines up to date (`tools/update_engines.py`).
- [`docs/CHANGES.md`](docs/CHANGES.md): what changed in each version; a line for every change (the version goes up each time).
- [`docs/AGENTS.md`](docs/AGENTS.md): what AI agents can do in Kalibre through MCP (0099); add a tool there when you add one.

Write to all of them when something changes.

## Working in it

```sh
# the core and its tests: seconds, no JUCE
cmake -B build-core -G Ninja -DKALIBRE_BUILD_APP=OFF && cmake --build build-core && ./build-core/KalibreTests
# everything (fetches JUCE 9.0.5, the CLAP SDK, Signalsmith Stretch)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build
xvfb-run -a -s "-screen 0 1600x1000x24 -extension MIT-SHM" ./build/KalibreAppTests_artefacts/Debug/KalibreAppTests
xvfb-run -a -s "-screen 0 1600x1000x24 -extension MIT-SHM" ./build/KalibreRender_artefacts/Debug/KalibreRender renders 1
```

- Look at a render after any drawing change (0023).
- Drive the real app headless: `Xvfb :99`, `DISPLAY=:99`, `xdotool` (mousedown, several mousemoves, mouseup), `import -window root`.
- Linux needs: ladspa-sdk libjack-jackd2-dev libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libxi-dev libgl1-mesa-dev.
- The Mac app is built and tested on CI. After pushing, check the run.
- When fixing a bug, have a test fail first. Commit and push early.

## Rules

- **Every change raises the version** (`project(Kalibre VERSION …)` in
  `CMakeLists.txt`; 1.0.0 from 25 November, the musician's main DAW): the last
  number for a fix, the middle for something new, the first for a change to
  how songs are kept or Kalibre is used. Add its line to `docs/CHANGES.md`.
  Mac, Windows and Linux share it: one branch, one version (0137). Docs alone
  don't raise it (they change no Kalibre anyone runs).
- **A release** is a tag `v<version>` on `main` (`v1.1.0`): CI builds it and
  attaches Kalibre-macOS.dmg, Kalibre-Windows.zip and Kalibre-Linux.tar.gz
  to the release. This
  session's GitHub access can't push tags, so the musician makes the release
  (Releases > Draft a new release, tag `v…`, target `main`).

- **No JUCE in `Source/Core`.**
- **The audio thread doesn't allocate or lock**: data goes through `Swap`.
- **Where signals meet, sum at 64 bits** (0081); what should leave sound untouched is proved by a cancellation test (`Tests/App/TestNeutral.cpp`). Don't change a test to make Kalibre pass it.
  The graph's nodes play on several threads at once (0078): nothing a
  processor touches may be shared with another node, unless it `runsAlone`.
- **A gesture is one undo step**: views show a drag and call `Controller::apply` once.
- **Every colour from `Theme.h`**, read when drawing (themes change it, 0132), every word 7:1 in every theme, greys R < G < B in the first, the accent only for "on", red never for words.
- **Nothing important at the bottom of the window** but the transport, which the musician asked for there (0095); messages in the message line, at the top, which stays until dismissed.
- **No extra windows for the music** (0002): ask with `ui::present`, show in the lower zone or the browser. Settings and preferences may have a window of their own (0002 amended, e.g. the Plug-in Manager, 0047).
- **The piano roll's mouse rules are the user's** (0009): don't change them without asking.
- **Keys and places on screen through `ui::words()`** (0129): Option-click is Alt+click on Windows and Linux, the Finder File Explorer or the file manager (0139).
- Words on screen follow Apple's writing guidelines: Title Case on buttons and menus, an ellipsis when more input follows, "click", no "we".
