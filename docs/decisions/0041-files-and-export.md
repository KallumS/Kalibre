# 0041. Templates, collect, relink, archives, MP3, metadata, batch export, Final Cut XML

## Decision

`Core/ProjectTools` (tested) lists and rewrites every file a project uses, relinks by name, makes templates, writes ID3 tags and Final Cut XML; `App/ProjectFiles` moves the files.

- **Templates**: File > Save as Template (tracks and plug-ins, or with the music), File > New from Template.
- **Collect All Files and Save**: outside files are copied into the project's Audio folder.
- **Relink**: on opening, missing files are counted in the message line; File > Relink Missing Files searches a folder (and its folders) by name.
- **Archive**: a .zip with the project and every file.
- **MP3**: Shine (LGPL-2.0, fixed point), 320 kb/s joint stereo, with an ID3v2 tag.
- **Song Details**: title, artist, album, year, track, genre, ISRC, comment; written into WAV (RIFF INFO and ISRC) and MP3 (ID3).
- **Export Several at Once**: the mix as WAV and MP3, every track, the MIDI, into one dated folder.
- **Final Cut XML** (xmeml 5) out and in: the audio clips, a track a track, for DaVinci Resolve, Premiere and Logic.

## Consequences

- AAF and OMF aren't written: both need a large binary container format (the AAF SDK); Final Cut XML carries the same clips to the programs that matter here, and Pro Tools users can go through Resolve or stems.
- FLAC and AIFF exports carry no tags.
