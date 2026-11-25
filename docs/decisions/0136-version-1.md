# 0136. Version 1, and a version for every change

## Context

25 November: the musician will use Kalibre as their main DAW. They asked
for it to be marked as version 1, and for every change to raise the version.
It was 0.1.0 since the start, shown nowhere a musician would look (only in
crash reports, the activity log and to AI agents).

## Decision

- Kalibre is **1.0.0**. The version is set once, in `CMakeLists.txt`
  (`project(Kalibre VERSION …)`), and reaches the app (`KALIBRE_VERSION`),
  the Mac's app bundle, crash reports, the activity log, DAWproject files and
  the AI agents' server.
- **Every change raises it**: the last number for a fix, the middle for
  something new, the first for a change to how songs are kept or how Kalibre
  is used. CLAUDE.md says so, for every session.
- **docs/CHANGES.md**: a line for each version, in plain words.
- **Kalibre > About Kalibre** (first in the Kalibre menu, as on every Mac
  app) says the version, asked inside the window (0002).

A tag of the version (`v1.0.0`) makes CI publish a GitHub Release with the
.dmg (the workflow already does this for tags starting "v"); it's for the
musician to ask for, as a release is public.

## Consequences

- A change without a new version and a line in CHANGES.md isn't finished.
- The `Windows` branch takes the version when it's merged.
