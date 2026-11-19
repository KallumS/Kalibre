# 0072. The Articulation Manager: maps imported, chosen in the piano roll

## Context

Asked (17 November): articulation maps couldn't be found in the piano roll
(they were chosen in a track's right-click menu); choose them under the
piano roll's Velocity menu, and an Articulation Manager under File to import
maps, as Note Position Fixer (KallumS/Note-Position-Volume-Fixer) does. Its
`npvf_maps.lua` (0009 there) reads Cubase expression maps, Logic
articulation sets and Reaticulate banks, the formats BabylonWaves Art
Conductor and REAPER make, into lists of outputs: VSL's maps choose with two
to four keyswitches at once, Spitfire's with a CC, others with programs or
channels.

## Decision

- `Core/Articulations` reads the three formats, as `npvf_maps.lua` does (its
  test files are Kalibre's test files, unchanged): each articulation's
  outputs and channel. The first key, controller and program go in an
  articulation's places (0013); further outputs in `more`, all sent a
  millisecond before the note. Each gets a colour in turn.
- **A library** of maps kept between songs (Application Support/Kalibre/
  Articulation Maps.json). Importing puts each map there, one of the same
  name replaced.
- **File > Articulation Manager…**, a window of its own (a settings window,
  0002 amended, as the Plug-in Manager): the library's maps and the song's,
  what each articulation sends, Import…, Use on Track, Remove from Library.
- **The piano roll's Velocity ▾ menu** has Articulation Map: None, the song's
  maps, the library's, and the manager. Choosing one copies it into the song
  (once) and sets it on the track, one undo step; the articulation lane shows.

## Consequences

Not read, as in Note Position Fixer: Cubase's note changer, remote triggers,
Reaticulate's routing and its group-2 articulations' meaning (they come in
as ordinary ones). Dynamics (velocity or CC1) aren't used by Kalibre.
Delays are 0 on import; set them in the manager (0082). Tested on
the three test files, through playback, and in the window (render 24).
