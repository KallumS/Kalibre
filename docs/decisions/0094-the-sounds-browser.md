# 0094. The Sounds browser: the musician's samples and MIDI, scanned once, heard in time

## Context

Asked (19 November): a collapsible pane on the left with a browser of the
folders on the Mac. Folders and items (samples, MIDI) can be favourites; it
can show every item in one list, without folders; the order can be
shuffled, within folders or in the one list. Clicking a sample or MIDI file
plays it (MIDI through Kalibre's own synth), in time with the song's tempo,
with a way to turn that off. Every common format: FLAC, ALAC, AAC, MP3, WAV,
OGG. Drag an item into the song, or right-click it for a new track with the
synth or the sampler. Up and Down move through the items. The browser shows
a sample's waveform, a MIDI file's notes. Items are scanned the first time
they're seen, only in formats Kalibre reads, and only in folders the
musician chooses: asked the first time Kalibre opens, and changeable in the
settings.

The browser on the left (⌘B, View > Browser) was already collapsible, with
a Files tab that walked one folder at a time and previewed audio only, at
its own speed.

## Decision

- **The Files tab becomes Sounds** (`App/LibraryView`): the existing
  left-hand browser, not a second pane, so there's one place for plug-ins,
  sounds, presets and settings, and ⌘B still shows and hides it. Its width
  is now the musician's: drag its right edge (saved, and in workspaces).
  Asked next: dragged past its narrowest (240 points) it stops, and past
  half of that it shuts, leaving a 6-point bar at the window's left edge
  with three grip dots, as the lower zone's divider has; dragging the bar
  opens it, following the pointer, and a click on it opens it as wide as it
  was. No button: the edge and the bar are the controls.
- **The lower zone does the same, toward the bottom** (asked next): dragged
  below its shortest (120 points) it stops, past half of that it shuts,
  leaving a 6-point bar along the bottom with the same three dots; drag the
  bar up, or click it. Shut, it opens whenever one of its tabs is asked for
  (D, E, X, G, Y, the View menu, a new plug-in, Edit Samples), so a key
  never seems to do nothing. View > Lower Zone and a key command (no key
  until given one) show and hide it. The bar is a handle, not something to
  read, so "nothing important at the bottom" holds. Saved, and in
  workspaces.
- **The library's logic is Core** (`Core/Library`, no JUCE, tested): the
  index of what's been scanned, the settings (folders, favourites, the
  view), and `rows`, which turns the two into the list: in folders (the
  chosen folders at the top, their folders, then their items) or one list;
  A to Z or shuffled, the shuffle a hash of each path with a seed, so it
  stays put until asked again and items stay in their folders; Favourites
  Only (an item is a favourite itself or inside a favourite folder); a
  search. Folders with nothing Kalibre reads aren't shown.
- **Scanning** (`App/LibraryScanner`): on its own thread, through the chosen
  folders and every folder inside, only files whose extension this Mac's
  Kalibre can decode (`AudioFormatManager::registerBasicFormats`: on the Mac,
  Core Audio adds AAC, ALAC, M4A and CAF to WAV, AIFF, FLAC, Ogg and MP3) and
  MIDI. Each file's length, rate, tempo, a 160-column waveform (a long file
  sampled a window a column, so an album folder scans quickly) or its notes
  in beats. The index is saved (`Library Index.json`); a file is read again
  only when its size or date changes, and forgotten when it's gone. Every
  start scans again, which after the first time only reads what's new.
- **Tempo**: from the name ("Drums 96bpm", "BPM120"), or a WAV's ACID chunk
  (not a one-shot's), or a MIDI file's first tempo. The same reading is now
  used when a sample is imported into the song.
- **Previews** (`App/PreviewPlayer`): the sound is made off the audio
  thread, then handed over through `Swap`; the audio thread only copies. A
  sample is resampled to the device's rate and, with **Sync** on and its
  tempo known, stretched to the song's tempo by `AudioCache::render`
  (Signalsmith Stretch, as a warped clip). A sample whose tempo isn't known
  isn't stretched: guessing would stretch one-shots. MIDI is played through
  Kalibre Synth (offline, in blocks), at the song's tempo with Sync on, at
  its own without. While the song plays, a synced preview starts on the next
  beat. Previews are their own audio callback beside the engine's, as the
  old Files tab's were: heard whatever the song does, never recorded or
  exported. At most a minute of a file.
- **Keys**: Up and Down choose and play the next item (folders are passed
  over without a sound); Left and Right shut and open a folder (Left on an
  item goes to its folder); Return puts the item on the chosen track at the
  cursor, as double-click does.
- **Into the song**: dragging an item is a `file:` drag, which the
  arrangement already took from the Files tab. Right-click: **New Track
  with Kalibre Sampler** (a sample: the sampler with the sample loaded, no
  clip; MIDI: the clip, the sampler empty), **New Track with Kalibre Synth**
  (MIDI: the clip at the cursor), **New Track with the Sample** (the sample
  as a clip), **Put on the Chosen Track at the Cursor**. Each is one undo
  step: `Controller::importFile` takes a function that makes the track and
  its instrument inside the same `apply`.
- **The folders**: asked the first time Kalibre opens (a question over the
  window, `ui::present`: Choose Folders… or Not Now, asked once), and in
  **Kalibre > Library Folders…**, a settings window of its own (0002
  amended): add, remove, Look Again. Nothing outside the chosen folders is
  looked through.
- Settings and the index live in Kalibre's own folder (Application Support
  on the Mac), not in a song: favourites are the musician's, across songs.

## Consequences

- A sample's tempo is only known from its name or its ACID chunk. Loops
  without either play at their own speed; Sync can't stretch them.
- Previews are rendered before they play: a long sample, stretched, takes a
  moment to start (the stretch is the slow part). A minute at most.
- The first scan of a large library takes a while; the browser fills in as
  it goes, and says how far it's got.
- Tested: `Tests/TestLibrary.cpp` (the list's rules), `Tests/App/TestLibrary.cpp`
  (scanning WAV, FLAC, Ogg and MIDI, skipping other files and hidden ones;
  only changed files read again; the preview's stretch and MIDI timing; the
  player starting on the next beat; the keys; one undo step a new track;
  removing a folder). AAC and ALAC are decoded by the Mac's Core Audio, so
  they're only tried on the Mac.
