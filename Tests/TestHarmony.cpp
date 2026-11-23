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

TEST ("harmony: soloing a return or the folder holding the chords leaves the chord lane as it was (UAT)")
{
    auto p = withChords();
    const Id ret = edit::addTrack (p, TrackKind::Return, "Reverb");
    p.track (ret)->solo = true;
    CHECK_EQ (harmony::chordsAlong (p, 0, PPQ * 8).size(), size_t (2));
    p.track (ret)->solo = false;
    // A folder soloed: the tracks inside it are heard.
    const Id folder = edit::addTrack (p, TrackKind::Folder, "Band");
    for (auto& t : p.tracks) if (t.holdsClips()) t.parent = folder;
    p.track (folder)->solo = true;
    CHECK_EQ (harmony::chordsAlong (p, 0, PPQ * 8).size(), size_t (2));
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

TEST ("harmony: drums written in the drum grid or the Euclidean aren't read as chords")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Beat");
    const Id c = edit::addMidiClip (p, t, 0, PPQ * 4);
    for (int pitch : { 36, 38, 42, 51 })
    {
        Note n; n.start = 0; n.length = PPQ; n.pitch = pitch; n.from = NoteFrom::Drums;
        edit::addNote (p, c, n);
    }
    Note e; e.start = 0; e.length = PPQ; e.pitch = 39; e.from = NoteFrom::Euclid;
    edit::addNote (p, c, e);
    CHECK (harmony::soundingAt (p, 10).empty());
    // A step-sequenced or played note still counts.
    Note s; s.start = 0; s.length = PPQ; s.pitch = 60; s.from = NoteFrom::Steps;
    edit::addNote (p, c, s);
    CHECK (harmony::soundingAt (p, 10) == std::vector<int> ({ 60 }));
}

#include "Ideas.h"

TEST ("ideas: Midi Suggester's engine puts chords and a bass under Good Idea's motif, and a melody over the chords (0100)")
{
    ScaleChoice aMinor;
    aMinor.root = 13;    // A (scaleview::roots spells both: 13 is A)
    aMinor.scale = 1;    // ScaleView's natural minor
    const auto tune = ideas::motif (aMinor, 4, 4, 4, 4321);
    CHECK (tune.error.empty());
    CHECK (! tune.notes.empty());
    const auto h = ideas::chordsFor (tune.notes, PPQ * 16, 4, 4, 77);
    CHECK (h.error.empty());
    if (! h.error.empty()) std::printf ("    %s\n", h.error.c_str());
    CHECK (h.chords.size() >= 6);                 // a chord a bar, three notes or more
    // A bass note for each chord, together covering the four bars (a chord may hold for two).
    Tick covered = 0;
    for (auto& n : h.bass) covered += n.length;
    CHECK (h.bass.size() >= 2 && covered == PPQ * 16);
    CHECK (! h.names.empty());
    for (auto& n : h.bass) CHECK (n.pitch < 60);
    for (auto& n : h.chords) CHECK (n.start < PPQ * 16);
    const auto over = ideas::melodyOver (h.chords, PPQ * 16, 4, 4, 5);
    CHECK (over.error.empty());
    CHECK (over.notes.size() >= 4);
    // The same seed, the same music.
    CHECK (ideas::chordsFor (tune.notes, PPQ * 16, 4, 4, 77).names == h.names);
}
