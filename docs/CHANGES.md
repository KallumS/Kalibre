# What changed, version by version

Kalibre's version is in **Kalibre > About Kalibre** (and in every song's
activity log). Every change to Kalibre raises it: the last number for a fix
(1.0.1), the middle one for something new (1.1.0), the first for a change
that alters how songs are kept or how Kalibre is used (2.0.0). Each
version's line says what changed, in plain words; the decision records
(`docs/decisions/`) say why.

## 1.3.1 (26 November 2026)

- Fixed: Kalibre could quit unexpectedly while taking plug-ins' pictures on
  the Mac. Each plug-in is now let go of two seconds after its window has
  closed, as everywhere else in Kalibre (0140).
- When a plug-in has put its own crash handling in Kalibre's place, so
  Kalibre wrote no crash report, the next start brings in macOS's report
  instead, and says which plug-in's picture was being taken.

## 1.3.0 (26 November 2026)

- Kalibre for Linux (0139): **Kalibre-Linux.tar.gz** beside the Mac's and
  Windows' downloads, with an installer for the applications menu. VST3,
  LV2, CLAP and LADSPA plug-ins; JACK and ALSA; Linux's keys and words.
  On the Mac and Windows nothing changes.

## 1.2.0 (26 November 2026)

- Plug-in pictures (0138): each plug-in can have a picture of its window,
  shown under its name in the browser's Plug-ins tab. The Plug-in Manager's
  **Take Pictures** opens each plug-in in turn, takes its picture and closes
  it; the camera button on a plug-in's panel takes a new one. **Pictures**,
  beside Scan, hides them. Kept in Kalibre's library, Plug-in Pictures.
- The Plug-ins tab's headings (MIDI Effects, Instruments, Audio Effects)
  fold away when clicked, and Kalibre remembers which.

## 1.1.0 (25 November 2026)

- The Mac and Windows versions are one: `main` holds both, and every build
  makes the Mac's .dmg and Windows' .zip from the same code (0137). On the
  Mac nothing changes. On Windows, About Kalibre is in the Help menu.

## 1.0.0 (25 November 2026)

Kalibre is the musician's main DAW from here. Everything up to now, as
`docs/FEATURES.md` lists it, including, from the last days:

- The full UAT: every device and every part of Kalibre used to make a song,
  and every finding fixed or answered (0133, 0134).
- Kalibre's own devices in every slot that takes a plug-in.
- The activity log beside each song, for record labels (0135).
- View > Text Size from 50% to 200%.
- Random's drums on Kalibre Drums; Shift-Random's tracks never share a name;
  Steps' Random in the track's octave.
- About Kalibre, saying the version (0136).
