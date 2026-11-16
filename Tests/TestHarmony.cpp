#include "Check.h"
#include "Edit.h"
#include "Harmony.h"

using namespace kal;

static Project withChords()
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Piano");
    const Id c = edit::addMidiClip (p, t, 0, PPQ * 8);
    for (int pitch : { 48, 52, 55, 59 }) edit::addNote (p, c, { 0, 0, PPQ * 4, pitch, 90 });     // Cmaj7
    for (int pitch : { 53, 57, 60 }) edit::addNote (p, c, { 0, PPQ * 4, PPQ * 4, pitch, 90 });    // F
    const Id d = edit::addTrack (p, TrackKind::Track, "Drums");
    const Id dc = edit::addMidiClip (p, d, 0, PPQ * 8);
    edit::addNote (p, dc, { 0, 0, PPQ, 37, 90 });
    edit::addNote (p, dc, { 0, 0, PPQ, 38, 90 });
    return p;
}

TEST ("harmony: the chord sounding at the playhead")
{
    const auto p = withChords();
    CHECK_EQ (harmony::chordName (harmony::soundingAt (p, PPQ), p.scale), std::string ("Cmaj7"));
    CHECK_EQ (harmony::chordName (harmony::soundingAt (p, PPQ * 5), p.scale), std::string ("F"));
}

TEST ("harmony: drum tracks are not harmony")
{
    const auto p = withChords();
    const auto s = harmony::soundingAt (p, 0);
    CHECK (std::find (s.begin(), s.end(), 37) == s.end());
}

TEST ("harmony: the chord lane merges a held chord")
{
    const auto p = withChords();
    const auto spans = harmony::chordsAlong (p, 0, PPQ * 8);
    CHECK_EQ (spans.size(), size_t (2));
    CHECK_EQ (spans[0].name, std::string ("Cmaj7"));
    CHECK_EQ (spans[0].end, PPQ * 4);
    CHECK_EQ (spans[1].name, std::string ("F"));
}

TEST ("harmony: scale membership and spelling")
{
    ScaleChoice s;   // C Major
    CHECK (harmony::inScale (s, 64));
    CHECK (! harmony::inScale (s, 61));
    s.root = 5;      // Eb
    s.scale = 0;
    CHECK (harmony::inScale (s, 63));
    CHECK_EQ (harmony::scaleName (s), std::string ("Eb Major"));
    CHECK_EQ (harmony::noteName (s, 58), std::string ("Bb3"));
}

TEST ("harmony: key finding prefers the obvious key")
{
    std::vector<harmony::TimedPitch> notes;
    Tick t = 0;
    for (int p : { 60, 62, 64, 65, 67, 69, 71, 72, 67, 64, 60 }) { notes.push_back ({ p, t, PPQ }); t += PPQ; }
    const auto ranked = harmony::rankKeys (notes);
    CHECK_EQ (ranked.front().root, 0);
    CHECK_EQ (ranked.front().scale, 0);
}
