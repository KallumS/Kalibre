# The colour scheme

Kalibre uses the house scheme, the same as Good Idea, Starting Blocks, Midi
Suggester, Midi Variator, Midi Catalogue, ScaleView and VST-Morpher: a dark
cool-grey ground, a light grey for controls raised off it, and one yellow for
whatever is switched on. Every value lives in `Source/App/Theme.h`, named by
role; nothing else in the code writes a colour. Change one there, change it
here too.

This is the first theme, Midnight Yellow. **View > Themes** has ten more
(decision 0132), each made in `Theme.cpp` from a hue, how strongly the greys
lean to it, an accent and warning words, on the same ramp of lightness
(inverted for the white one, Porcelain), with every word moved until it reads
at 7:1. In those, the greys lean to the theme's hue instead of blue. Words on
a track's colour go on `theme::underWords` so they read in every theme.

## The four that carry it

| | value | what it is |
| --- | --- | --- |
| Accent | `#FFF200` | whatever is on: a chosen button (no bar under its label since 16 November: the user's choice), chosen notes and clips, the track being played (a bar on its header), a hit in the drum grid, a sounding chord |
| Controls | `#A9AFBA` | buttons, slider thumbs, knob arcs |
| Ground | `#23272E` | the window |
| Ink | `#14171C` | the text on every button, chosen or not |

## The ramp (R < G < B in every one)

| | role |
| --- | --- |
| `#111419` | the arrangement's and the piano roll's ground |
| `#14171C` | ink; the piano roll's rows outside the scale |
| `#181B21` | piano roll white-key rows (no scale shaded) |
| `#1A1D23` | sunken: lanes, the lower zone's panels, text fields |
| `#1E2228` | grid lines between beats |
| `#1F2A2E` / `#24343A` | a scale's rows / its root's row: ScaleView's teal, darkened into a ground |
| `#22262D` | frame hover |
| `#2A2F37` | raised: menus, tooltips, panels lighter than the window |
| `#3A404A` | rules, bar lines |
| `#6D7581` | scrollbar thumb (3.6:1) |
| `#8A919C` | edges of things to grab (4.7:1); never words |
| `#8F96A2` / `#C0C6CF` | control held / hover |
| `#B0B6C0` | secondary text (7.4:1) |
| `#BFC5CE` | secondary words on menus (7.8:1 on raised) |
| `#DDE1E7` | body text (11.4:1) |
| `#F2F4F7` | playhead, focus ring |

## Off the ramp

| | role |
| --- | --- |
| `#D2483F` | marks only: recording, clipping, notes about to be removed |
| `#FF9E96` | warnings written as words (7.6:1) |
| `#7FD29A` / `#F2C46D` | meters below -6 dB / -6 to 0 dB |
| Track colours | eight, chosen per track; they're content, so a few lean warm |

## Rules carried over

- **Keep the greys blue.** A neutral grey looks right in a diff and flat on screen beside the yellow.
- **Light controls force dark text.** Every button takes the ink.
- **The accent means "on" and nothing else.** The focus ring is near-white, warnings are red.
