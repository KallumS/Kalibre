# 0057. MusicXML in and out

## Context

The user asked for MusicXML import and export (16 November), pointing at
Noterator, Miderator and Middaw. Noterator and Miderator read and write
MusicXML in their cores, with a small XML parser of their own (Noterator's
decision 0021); their writer is built on Noterator's engraver, which Kalibre
doesn't have.

## Decision

- `Core/Xml.*` is Miderator's XML reader and writer, as it is (no JUCE).
- `Core/MusicXml.*` reads as Miderator does: partwise files; divisions,
  chords, backup and forward; ties joined into one note; grace and cue notes
  skipped; transposing parts sounded at pitch; a pickup bar moved to the end
  of bar one; drums from their MIDI instruments; tempo, time signatures and
  key from the first part; velocity from `dynamics`.
- It writes with a small layout of its own on Kalibre's notation spelling
  (0020): each track a part, its MIDI clips as they play (loops included);
  notes squared to 1/32s for the page; overlapping notes in up to four
  voices; ties over bar lines; plain and dotted values, tied; the key from
  the song's scale, the bars from its time signatures, the tempo from its map;
  treble or bass clef by range; velocity as `dynamics`.
- File > Export MusicXML…: the chosen tracks (two or more) with MIDI, or
  every track with MIDI. File > Import MusicXML… (.musicxml, .xml or zipped
  .mxl): a track for each part, one clip each. Into an empty song the score's
  tempo, time signatures, key and title come too; into one with music, the
  parts go in at the bar the cursor is in. One undo step.

## Consequences

- Triplets come out as the nearest 1/32s; dynamics markings, articulations,
  slurs, lyrics and layout aren't carried either way. Timewise files are
  refused with a reason.
- Tested: a round trip with chords, a second voice, ties, a change to 3/4,
  tempo and an Eb minor key (and every voice of every bar adds up to the
  bar); a hand-written file with a pickup, a Bb clarinet, a grace note, a tie
  and a kick drum; keys both ways for every signature; an .mxl into an empty
  song.
