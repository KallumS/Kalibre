# Kalibre

A DAW for Apple silicon Macs (JUCE 9, C++20), everything in one window.
Built for one musician, who is not a developer: explain in plain words, show
renders of what changed, and make sure every change reaches them as the CI's
`.dmg` (Actions > Build > Kalibre-macOS).

## Where the reasons are

- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md): the shape, how sound flows, rules easy to break. Start here.
- [`docs/decisions/`](docs/decisions/README.md): one record per choice, by number: **(0009)** is the piano roll's mouse rules.
- [`docs/FEATURES.md`](docs/FEATURES.md): what's done, what's partly done, what isn't. Keep it true.
- [`docs/USER-GUIDE.md`](docs/USER-GUIDE.md): for the musician: every function, menu, key and click, with pictures (`docs/guide/`, from KalibreRender). Change it when what they see or press changes.
- [`docs/DEVICES.md`](docs/DEVICES.md): every built-in device in detail, from the family's own READMEs (clone them from KallumS), with what differs in Kalibre.
- [`docs/sessions/`](docs/sessions/): one log per working session.
- [`docs/UPDATING-ENGINES.md`](docs/UPDATING-ENGINES.md): bringing the family's engines up to date (`tools/update_engines.py`).

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
- Linux needs: libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libxi-dev libgl1-mesa-dev.
- The Mac app is built and tested on CI. After pushing, check the run.
- When fixing a bug, have a test fail first. Commit and push early.

## Rules

- **No JUCE in `Source/Core`.**
- **The audio thread doesn't allocate or lock**: data goes through `Swap`.
- **Where signals meet, sum at 64 bits** (0081); what should leave sound untouched is proved by a cancellation test (`Tests/App/TestNeutral.cpp`). Don't change a test to make Kalibre pass it.
  The graph's nodes play on several threads at once (0078): nothing a
  processor touches may be shared with another node, unless it `runsAlone`.
- **A gesture is one undo step**: views show a drag and call `Controller::apply` once.
- **Every colour from `Theme.h`**, every word 7:1, greys R < G < B, the accent only for "on", red never for words.
- **Nothing important at the bottom of the window**; messages in the message line, which stays until dismissed.
- **No extra windows for the music** (0002): ask with `ui::present`, show in the lower zone or the browser. Settings and preferences may have a window of their own (0002 amended, e.g. the Plug-in Manager, 0047).
- **The piano roll's mouse rules are the user's** (0009): don't change them without asking.
- Words on screen follow Apple's writing guidelines: Title Case on buttons and menus, an ellipsis when more input follows, "click", no "we".
