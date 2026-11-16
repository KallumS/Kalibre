/*
    MusicXml - tracks in and out of the format every notation program shares:
    Dorico, MuseScore, Sibelius, Finale, Notion (decision 0057).

    Writing: each chosen track's MIDI clips (the version playing) become a
    part. Notes are squared to the nearest 1/32 for the page, notes that
    overlap go into voices of their own (four at most), a note crossing a bar
    line is tied, lengths are written as plain or dotted values, tied. The
    key comes from the song's scale, the bars from its time signatures, the
    tempo from its tempo map; each part is in treble or bass clef by its
    range. Spelling is Kalibre's own (notation::spell). Triplets come out as
    the nearest 1/32s.

    Reading is Miderator's and Noterator's (decision 0021 there): partwise
    files, divisions, chords, backup and forward, ties joined into one note,
    grace and cue notes skipped, transposing instruments sounded at pitch, a
    pickup bar moved to the end of bar one, drums from their MIDI instruments.
    The song's tempo, time signatures and key are read from the first part.

    No JUCE in here: the app opens .mxl (zipped) files and hands over the text.
*/

#pragma once

#include "Model.h"

#include <optional>
#include <string>
#include <vector>

namespace kal::musicxml
{

// The tracks (in order) as a MusicXML score, partwise.
std::string write (const Project&, const std::vector<Id>& tracks);

struct Part
{
    std::string name;
    int program = -1;              // General MIDI, 1-based as in the file; -1 when absent
    bool percussion = false;
    std::vector<Note> notes;       // song ticks from the start of the score
};

struct Score
{
    bool ok = false;
    std::string error;
    std::string title;
    std::vector<Part> parts;
    std::vector<TempoPoint> tempos;
    std::vector<Meter> meters;
    std::optional<ScaleChoice> key;
    Tick length = 0;               // to the end of the last bar
};

Score read (const std::string& text);

// The key signature (sharps positive, flats negative) and mode for a scale, and back.
int fifthsFor (const ScaleChoice&);
bool isMinor (const ScaleChoice&);
ScaleChoice scaleFor (int fifths, bool minor);

} // namespace kal::musicxml
