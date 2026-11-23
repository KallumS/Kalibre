# Full UAT, 23 November 2026: every device, every part of Kalibre

Asked: use every feature in every device and in Kalibre itself to make a
song on the Linux machine; it needn't be a good song. Kalibre ran on a
virtual screen from a first start, driven by mouse and keys (`xdotool`), its
pictures taken as it went. What was fixed: [decision 0133](../../decisions/0133-what-the-full-uat-found.md).

**How it ran, and what that means.** Linux has no sound card here, so the
first half ran with no audio device: nothing played. For playing,
recording, the mixer's meters and the family's own windows, a second build
with JACK ran on JACK's dummy device (a timer standing in for a sound card).
Both were Debug builds, much slower than the Mac's, especially at drawing
the family's ReaImGui tools. Your Mac is still the real test.

The song: A minor, 110 BPM, about 14 tracks. [The mix as exported](full-uat-mix-before-fixes.mp3)
(it's far too loud: nothing was mixed, and it clips; see finding 52).

## What was used

| Area | Used |
| --- | --- |
| First start | The folders question, Choose Folders…, 15 sounds found |
| Studio window | Clock and Song Position, MTC, MMC, Obey MMC, Ableton Link, OSC, AI Agents on |
| Top bar | Tempo typed, Tap, Time Signature, Grid, Scale (Root, Scale, Random Scale), position in four formats, Follow and Follow: Page, song versions 1 and 2, Random, Shift-Random |
| View | Themes (Porcelain and back), Text Size 150%, Tempo Lane, Chord Lane, Section Lane, Zoom to Fit, browser shut and open |
| Kalibre Drums | Random Samples, + Pad, Random Beat, steps clicked, Option-clicked, painted, 32 Steps, Swing, pad mute, volume and pan, Rename, Choke Group, Drag to Timeline |
| Drum grid | Shift-click, right-click, Swing, Gate, Velocity, Note Chance, Random Timing, Repeat, Random, Undo |
| Euclidean | Steps, Hits, Rotate, Add Drum, a row's drum, Random |
| Drum Pads | A kick dragged from the Sounds tab, Random Samples, Solo, Unsolo All, volume, pan, Choke Group, the map, Plug-in ▾ |
| Kalibre Synth | Factory Presets (Acid Bass), Set A/B, Save Preset…, Random, B, power off and on, … menu, Automate the Last Touched Control, Modulate (LFO, its shapes) |
| Piano roll | Notes drawn, every arrow key, Velocity lane, Note Delay, Pitch Bend, CC lanes, Select ▾ (Every Beat), Quantise, Legato, Humanise, every tool in the toolbox |
| Notation, Steps | Notes on the staff, note values; Steps' Random, pitch and velocity drags, tie |
| Clips | A MIDI file from the Sounds tab (New Track with Kalibre Synth), Detect Chords, Set the Loop, Reverse, Invert, Create Alias Clip and its copy, Explode Chords, Fix Note Timings and Volume… |
| Chords' devices | ScaleView (its scale), Note Splitter (3 outputs, New Tracks), Instrument Creator (its own window, Surprise Me, a part), Kalibre Sampler (Pad A3 dropped, Keys, Slices, Granular, Loop, Random Sample), Reese Basser (presets, tabs, Mutate) |
| Lead | Trombolese, notes from an AI agent, AutoCC (Auto on a track named Violin), CC 7 lane, an articulation map, the articulation lane |
| Audio | A loop dragged to a new track (warped to the tempo), Warp Granular, a warp marker, sample editor (range, Reverse, Fade In, Spectrum, Remove DC, Gain, Quantise Audio), Split at Transients (Sensitivity), Consolidate, Freeze and Unfreeze |
| Tracks | Rename, New Version, Track Delay (-125 ms), Group (two tracks move together, one undo step), a Folder (dragged in, folded), Return with Reverberator, a send from I/O, Summing, Cue |
| Playing and recording | Play on JACK, Click, Count-in, Metronome Clicks, record onto the Summing track, takes, Use This Take, Flatten the Comp, Ramp the Tempo to Here, 3/4 at bar 9 |
| Mixer | Faders, pan, M, S, Ø, Mono, double-click resets, send knob, Loudness, meters while playing; Meters tab (spectrum, phase, oscilloscope) |
| Arrangement | Sections (add, rename, duplicate, Shift-drag), a marker, Insert Bars, Delete the Loop Range's Bars, Ripple Editing, Range, Split, Glue, Mute tools |
| More devices | Container, Multiband (4 bands, a crossover dragged), Stereo Split (an edge dragged, Middle and Sides), X/Y Morph (two sample corners, the dot), Good Idea (New Idea, Audition, Insert at Cursor), the device rack's Random |
| Agents and scripts | get_song, add_plugin, create_track, add_notes, edit_clip, delete_clip, transport, run_script, undo, update_track; Scripts: Four on the Floor |
| Files and windows | Save, Open, Song Details, Export the Mix (WAV), Keys window, Plug-in Manager, Articulation Manager, Library Folders, the Battle Pass (the timer clicked seven times) |

**Not tried here** (and why): step input, MIDI learn, Capture MIDI and the
tuner (no MIDI keyboard or sound in); the Plug-in Manager's scanning, sidechains,
instruments' other outputs, ARA, Hardware Insert's Ping, Offline Effects'
Render, the Container's and Multiband's own plug-ins (no plug-ins on Linux:
see 31); Midi Evolver, Midular, MIDI Production Center, Starting Blocks,
Midi Catalogue, Orchestration Helper and Instant Virtual Instrument were
added but not played with (the Debug build stalled drawing them, see 46);
Import Video, MusicXML, DAWproject and Final Cut XML in and out, Collect,
Archive, templates, backups and workspaces (each has its automated tests;
there was no time left to drive them by hand).

## Every finding

**Fixed** (decision 0133), each with a test that failed first:

| # | Found | Done |
| --- | --- | --- |
| 4 | A question's field didn't have the keys: typing a section's name soloed a track (S) and **started recording (R)** | The field has the keys, its text chosen |
| 39 | **Recording with Count-in put the take a bar late**: its first bar was the count-in's silence | Written from where the count-in ends; a take from the start no longer starts a block late either |
| 40 | **A track armed for MIDI recorded its own clip back** over itself, every note doubled | Its own clips' notes are left out |
| 27 | Explode Chords copied the Note Splitter onto every voice's track, which sent each voice on again | It stays behind |
| 12 | The last touched control was set by A/B, Undo, presets (Automate made a Volume lane after Cutoff was turned) | Putting a state back isn't touching |
| 47 | One agent request stuck waiting held up every later one, for ever | Answered "busy" after five minutes |
| 52 | The exported WAV clipped (the mix at +12 dBTP) and nothing said so | The message line says so, and by how much |
| 41 | Keys shown as "home", "end"; forward Delete as ⌫ | ↖, ↘, ⌦ on the Mac |
| 22 | The agent's reply "Added 1 notes" | "Added 1 note" |
| 42 | The top bar's "Tempo (chan" | "Tempo (changes)" fits |
| 3 | The Scale menu's heading "Root" over Root and Scale | "Key" |
| 2 | The message line said "Looking through the folder" after the scan was done | It says how many sounds were found |
| 21 | A MIDI clip's menu had no Transpose, though the guide said so | Added |
| 15 | The guide said to right-click a CC lane for its menu; it's the lane's name | Guide corrected |

**Asked, and answered:**

| # | Found | Your answer |
| --- | --- | --- |
| 48 | Clicking an empty part of a lane chose the track, and Delete then deleted the whole track | Keep it: Delete deletes the chosen tracks, as the guide says |
| 10, 31 | No slot that takes a plug-in offered Kalibre's own devices | Yes: every slot now lists Kalibre's own of its kind (0134) |
| 36 | Scrolling over the mixer turns the fader or knob under the pointer | Keep it |

Suggestions, not asked: 49 (Shift-Random's track names, and the Sampler
where Kalibre Drums would do), 18 (Steps' Random ignores the octave the bass
plays in).

**Fixed later the same day** ([decision 0134](../../decisions/0134-the-uats-smaller-findings.md)), each with a test that failed first:

| # | Found | Done |
| --- | --- | --- |
| 5 | "Theme: Porcelain." showed Undo, which would undo the Scale | No Undo beside it |
| 6 | Kalibre Drums: right-clicking a pad's name showed nothing | Its menu opens |
| 7 | Kalibre Drums' Swing wasn't carried into the clip dragged to the timeline | Swing and Gate go with it |
| 8 | Euclidean: the dials had no names; the last row's sat left | Steps, Hits, Rotate over each; in line |
| 9 | Drum Pads in a short zone: the bottom row cut off | All sixteen show |
| 11, 23, 44 | Devices with no name in the rack (switched off, ScaleView, Note Splitter, X/Y Morph); ScaleView didn't take the song's scale | Panels wide enough for their names; ScaleView starts on the song's scale |
| 13 | The LFO's knobs had names only in tooltips; its picture stayed a sine | Named over each; drawn again when the shape changes |
| 14 | At 1440 wide with the browser open, CC Lanes… and Auto were pushed off the editor bar | The tools go on a second row |
| 16 | The lane under the notes couldn't be cleared there | Its menu has Clear Lane |
| 17 | Notation: a bar in a tenth of the staff; Humanise made 1/32 rests and tied fragments | Bars fill the staff; notes read on the nearest 1/32 or triplet |
| 19 | Steps: a double-click to tie made three undo steps | One |
| 20 | A 4-bar MIDI file put at beat 3 became 17 beats long | 16: whole bars from where it's put |
| 24, 25, 26 | The Sampler and Instrument Creator squeezed in a short zone; Loop needed a 200-point drag | One row of knobs when short; a short drag switches Off/On |
| 29 | The articulation lane's words on its mint colour | The colour deepened to 7:1 under the words, in every theme |
| 30 | Split at Transients made 16 clips where the Sample Editor showed 15 hits | A hit at the very start is counted; the message says clips and hits |
| 32 | A frozen track looked as before | "Frozen" under its name |
| 35 | The Master strip showed no meter while playing | Its level was there; its strip had scrolled past the end. Master and Cue now stay in sight |
| 37 | Nothing grew on the armed track while recording | The take grows, in the arm's colour |
| 38 | The chord lane's playing chord drawn in pieces | Drawn whole |
| 43 | Soloing a return emptied the chord lane | It doesn't |
| 45 | Good Idea and the ReaImGui tools in a 400×300 box | They fill their panel |
| 46 | (Debug) three ReaImGui tools kept Kalibre busy drawing | They draw only while on screen, and slower after a slow frame. Still to watch on the Mac |
| 51 | Export: the save panel stayed frozen, no progress | The panel goes, then the message line counts up |
| 53 | A marker's name over the bar number | After it |

Not faults: 1 (the Studio window's tooltip, cut off on the virtual screen,
which has no window manager: the main window drew over it; "Share Start and
Stop" had no tooltip and now has one), 33 (Ctrl+Q on Linux only), 34 (True Peak is written in the
salmon kept for warning words, not red).

## Pictures

![First start](01-first-start-folders-question.png)
![15 sounds found](02-fifteen-sounds-found.png)
![The Studio window: agents, Link, OSC](03-studio-window-agents-link-osc.png)
![The Tempo question](04-tempo-question-field-not-ready.png)
![Porcelain, with the tempo lane](05-porcelain-theme-tempo-lane.png)
![Text at 150%](06-text-at-150-percent.png)
![Kalibre Drums](07-kalibre-drums.png)
![Random Samples and Random Beat](08-random-samples-and-beat-80-percent.png)
![A pad's menu only from its sound's label](09-pad-menu-only-from-sound-label.png)
![The beat dragged to the timeline](10-beat-dragged-to-timeline.png)
![The drum grid: Repeat, Swing, Gate](11-drum-grid-repeat-swing-gate.png)
![Euclidean: unnamed dials](12-euclidean-dials-unnamed.png)
![Drum Pads: the bottom row cut off](13-drum-pads-bottom-row-cut-off.png)
![Drum Pads' map](14-drum-pads-map.png)
![Kalibre Synth: Acid Bass](15-synth-factory-preset-acid-bass.png)
![Switched off: no name](16-device-switched-off-loses-name.png)
![The LFO's knobs](17-lfo-knobs-unnamed.png)
![The bass drawn](18-bass-drawn-in-piano-roll.png)
![CC Lanes… and Auto, only with the browser shut](19-editor-bar-cc-lanes-auto-only-without-browser.png)
![The toolbox in the piano roll](20-toolbox-in-piano-roll.png)
![Notation after Humanise](21-notation-after-humanise.png)
![Steps' Random in octave 4](22-steps-random-in-octave-four.png)
![Chords detected](23-chords-detected.png)
![Alias clips](24-alias-clips.png)
![Note Splitter and ScaleView](25-note-splitter-scaleview-nameless.png)
![Instrument Creator in its own window](26-instrument-creator-own-window.png)
![The Sampler squeezed](27-sampler-squeezed-in-short-zone.png)
![The Sampler at a taller zone](28-sampler-at-a-taller-zone.png)
![Reese Basser](29-reese-basser.png)
![AutoCC](30-autocc-on-violin.png)
![The articulation lane](31-articulation-lane.png)
![Fix Note Timings and Volume](32-fix-note-timings-result.png)
![The sample editor](33-sample-editor-spectrum.png)
![Split at Transients](34-split-at-transients-16-clips.png)
![The mixer, playing](35-mixer-playing-on-jack.png)
![Recording; the chord lane in pieces](36-recording-chord-lane-broken.png)
![The count-in take, a bar late](37-count-in-take-a-bar-late.png)
![Takes; the Violin's notes doubled](38-takes-and-midi-doubled.png)
![Multiband and Stereo Split](39-multiband-and-stereo-split.png)
![X/Y Morph](40-xy-morph-name-cut.png)
![Good Idea, its grip dragged](41-good-idea-grip-dragged.png)
![A section duplicated](42-section-duplicated.png)
![A section moved](43-section-moved-marker-over-13.png)
![Song version 2](44-song-version-two.png)
![Shift-Random](45-shift-random-added-tracks.png)
![The Keys window](46-keys-window-home-end.png)
![The Plug-in Manager](47-plugin-manager.png)
![The Meters tab](48-meters-playing.png)
![The Battle Pass](49-battle-pass.png)
