# UAT, 22 November 2026: a song made on the Linux machine

Asked: use Kalibre as it is to make a simple song, with a variety of devices
and ways of putting notes in, as a user acceptance test. Kalibre ran on a
virtual screen with no settings (a first start), driven by mouse and keys
(`xdotool`), its pictures taken as it went. Everything found was then fixed:
[decision 0109](../../decisions/0109-what-the-uat-song-found.md).

The song: four bars in A minor at 100 BPM, five tracks.

| Track | Device | How its notes went in |
| --- | --- | --- |
| Drums | Kalibre Drums | Its steps clicked; Random Samples; Drag to Timeline; ⌘D |
| Bass | Kalibre Synth | Drawn with the mouse in the piano roll |
| Chords | Kalibre Sampler + Reverberator | A MIDI file dragged from the Sounds tab; Random Sample, then a pad dropped on it |
| Lead | Trombolese + Reverberator | Written by an AI agent over MCP |
| Arp | Kalibre Sampler | A Lua script ("Arpeggio from the Chords") |

Listen: [the mix before the fixes](uat-song-mix-before-fixes.mp3) (the lead
blows up in bar 4), [after](uat-song-mix-after-fixes.mp3).

## First start

The first question, the folders chosen, the sounds found; the tempo typed.

![First start: where are your samples?](01-first-run-folders-question.png)
![Choosing the folder](02-choose-folders.png)
![Nine sounds found](03-sounds-found.png)
![The tempo, typed](04-tempo-dialog.png)

## Drums

Kalibre's own devices in + Add; Kalibre Drums, its eighth pad (Crash) cut off
(fixed: the pads scroll); Random Samples gave each pad its kind; the beat
clicked in; dragged to the timeline (it opened in the piano roll, and landed a
sixteenth late: fixed), the drum grid, four bars by ⌘D.

![+ Add](05-add-menu.png)
![Kalibre's own devices](06-kalibres-own-devices.png)
![Kalibre Drums: the Crash pad cut off](07-kalibre-drums-crash-pad-cut-off.png)
![Random Samples and the beat](08-random-samples-and-clicked-beat.png)
![Dragged to the timeline](09-drag-to-timeline-opens-piano-roll.png)
![The drum grid](10-drum-grid.png)
![Four bars](11-four-bars-of-drums.png)
![Rename from the track's menu (double-click didn't: fixed)](12-track-menu-rename.png)
![The Scale menu](13-scale-menu.png)

## Bass

Kalibre Synth; no presets (fixed: eleven); its headings moved down after a knob
was turned, and Cutoff read 502.246 Hz (fixed); the new clip opened in the drum
grid (fixed); nine keys in the piano roll (fixed); the bass drawn.

![Kalibre Synth](14-kalibre-synth.png)
![No presets](15-synth-no-presets.png)
![Headings moved, 502.246 Hz](16-synth-headings-moved-after-a-knob-turn.png)
![A bass clip in the drum grid](17-bass-clip-opened-in-drum-grid.png)
![Nine keys](18-piano-roll-nine-keys.png)
![The bass drawn](19-bass-drawn-in-piano-roll.png)
![Notes in the chosen track's colour (fixed)](20-track-menu-over-roll.png)

## Chords

Kalibre Sampler, its headings over its labels (fixed); Random Sample; the
Sounds tab (a held pad "about 120 BPM", its note named A2 where it's A3:
fixed); a pad dropped on the Sampler; Look Again found the MIDI file (now
looked for on coming back to Kalibre); the MIDI file dragged to the track.

![Sampler headings over labels](21-sampler-headings-over-labels.png)
![Random Sample](22-random-sample-loop-loaded.png)
![The Sounds tab](23-sounds-tab-keys-folder.png)
![A pad dropped on the Sampler](24-pad-dropped-on-sampler.png)
![Library Folders](25-library-folders.png)
![Look Again](26-look-again-finds-midi-file.png)
![The MIDI file on the Chords track](27-midi-file-dragged-to-chords.png)

## Lead, Arp, mix and export

AI agents switched on in the Studio tab; the Scripts menu; the Lua arpeggio
made the fifth track; the Sampler shrunk out of reading in a short zone
(fixed); octaves named two ways ("Pluck C4" read C3 in the Sounds tab, C4 on
the Sampler: fixed); the mixer with the agent's levels; saved; exported: a true
peak of +22.9 dBTP from Trombolese's blow-up, and 38 seconds for a 10-second
song (both fixed).

![Studio tab: AI agents](28-studio-tab-ai-agents.png)
![Scripts](29-scripts-menu.png)
![Five tracks](30-lua-arpeggio-five-tracks.png)
![The Sampler shrunk](31-sampler-shrunk-out-of-reading.png)
![Octaves two ways](32-octave-names-two-ways.png)
![The mixer](33-mixer-levels-by-agent.png)
![The File menu](34-file-menu.png)
![Export the Mix](35-export-the-mix.png)
![Too loud](36-export-too-loud.png)

## After the fixes

The song opened again: the pads scroll; ⇧⌘E with nothing clicked; the Sampler
keeps its width and its knobs can be turned; Cutoff reads 502 Hz with the
headings in place and + Add in view; Factory Presets; a double-click renames.
Exported again: 13.9 seconds, no blow-up.

![The song again](37-after-fixes-song-reopened-pads-scroll.png)
![A shortcut without a click](38-after-fixes-shortcut-without-click.png)
![The Sampler keeps its width](39-after-fixes-sampler-keeps-width.png)
![Its knobs can be turned](40-after-fixes-sampler-knobs-turnable.png)
![502 Hz](41-after-fixes-synth-502-hz.png)
![Factory Presets](42-after-fixes-factory-presets.png)
![Double-click renames](43-after-fixes-double-click-rename.png)
