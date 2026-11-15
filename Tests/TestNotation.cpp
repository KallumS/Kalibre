#include "Check.h"
#include "Edit.h"
#include "Notation.h"

using namespace kal;
using namespace kal::notation;

TEST ("notation: spelling follows the scale")
{
    ScaleChoice gMajor { 10, 0 };   // G
    auto s = spell (66, gMajor);
    CHECK_EQ (s.letter, 3);
    CHECK_EQ (s.accidental, 1);
    ScaleChoice dbMajor { 2, 0 };   // Db
    s = spell (66, dbMajor);
    CHECK_EQ (s.letter, 4);
    CHECK_EQ (s.accidental, -1);
}

TEST ("notation: staff steps and back")
{
    ScaleChoice c;
    CHECK_EQ (staffStep (spell (64, c), Clef::Treble), 0);      // E4, bottom line
    CHECK_EQ (staffStep (spell (77, c), Clef::Treble), 8);      // F5, top line
    CHECK_EQ (staffStep (spell (43, c), Clef::Bass), 0);        // G2
    CHECK_EQ (pitchAtStep (8, Clef::Treble, c), 77);
    ScaleChoice g { 10, 0 };
    CHECK_EQ (pitchAtStep (8, Clef::Treble, g), 78);            // F# in G major
}

TEST ("notation: lengths become note values")
{
    const auto d = split (PPQ * 3);
    CHECK_EQ (d.size(), size_t (1));
    CHECK (d[0].value == Value::Half && d[0].dotted);
    const auto e = split (PPQ * 5);
    CHECK_EQ (e.size(), size_t (2));
}

TEST ("notation: a bar with a note, a rest and a tie over the bar line")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Flute");
    const Id c = edit::addMidiClip (p, t, 0, PPQ * 8);
    edit::addNote (p, c, { 0, 0, PPQ, 72, 90 });
    edit::addNote (p, c, { 0, PPQ * 3, PPQ * 2, 74, 90 });
    const auto bars = layout (*p.clip (c), p.tempo, p.scale, Clef::Treble);
    CHECK_EQ (bars.size(), size_t (2));
    CHECK_EQ (bars[0].glyphs.size(), size_t (3));          // quarter, half rest, quarter tied
    CHECK (bars[0].glyphs[1].rest);
    CHECK (bars[0].glyphs[2].tieToNext);
    CHECK (! bars[1].glyphs[0].rest);
}
