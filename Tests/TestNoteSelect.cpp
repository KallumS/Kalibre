// Choosing notes by what they are, and Explode Chords (decision 0075).
#include "Check.h"
#include "Edit.h"
#include "NoteSelect.h"

using namespace kal;
using namespace kal::noteselect;

namespace
{
// Four chords a bar each: Cmaj7 (C E G B), Am (A C E, A in the bass), F/A (A C F), G9 (G B D F A).
Project song (Id& clip)
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Piano");
    clip = edit::addMidiClip (p, t, 0, PPQ * 16);
    const std::vector<std::vector<int>> chords { { 48, 52, 55, 59 }, { 45, 48, 52 }, { 45, 48, 53 }, { 43, 47, 50, 53, 57 } };
    for (size_t i = 0; i < chords.size(); ++i)
        for (int pitch : chords[i])
        {
            Note n; n.start = (Tick) i * PPQ * 4 + (pitch % 3) * 10; n.length = PPQ * 4; n.pitch = pitch;   // played in, a little spread
            edit::addNote (p, clip, n);
        }
    return p;
}

std::multiset<int> pitches (const Clip& c, const std::set<Id>& ids)
{
    std::multiset<int> out;
    for (auto& n : c.midi.notes) if (ids.count (n.id)) out.insert (n.pitch);
    return out;
}
} // namespace

TEST ("note select: the roots, thirds, fifths, sevenths and ninths of every chord")
{
    Id clip = 0;
    auto p = song (clip);
    const auto& c = *p.clip (clip);
    CHECK (pitches (c, choose (c, p.tempo, What::Root)) == std::multiset<int> ({ 48, 45, 53, 43 }));     // C, A, F (over A), G
    CHECK (pitches (c, choose (c, p.tempo, What::Third)) == std::multiset<int> ({ 52, 48, 45, 47 }));
    CHECK (pitches (c, choose (c, p.tempo, What::Fifth)) == std::multiset<int> ({ 55, 52, 48, 50 }));
    CHECK (pitches (c, choose (c, p.tempo, What::Seventh)) == std::multiset<int> ({ 59, 53 }));
    CHECK (pitches (c, choose (c, p.tempo, What::Ninth)) == std::multiset<int> ({ 57 }));
    CHECK (choose (c, p.tempo, What::Eleventh).empty());
    CHECK (choose (c, p.tempo, What::Thirteenth).empty());
    CHECK_EQ (rootOf ({ 60, 64, 67, 69 }), 0);           // C6 is C, not Am7
    CHECK_EQ (degreeOf (69, 0, { 60, 64, 67, 69 }), 13);
    CHECK_EQ (degreeOf (65, 0, { 60, 64, 67, 70, 65 }), 11);
}

TEST ("note select: on the beat and off it; the bottom and top lines")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Bass");
    const Id clip = edit::addMidiClip (p, t, PPQ * 4, PPQ * 4);
    for (Tick at : { Tick (0), Tick (PPQ / 2), Tick (PPQ), Tick (PPQ * 3 / 2), Tick (PPQ * 2 + 5) })
    {
        Note n; n.start = at; n.length = PPQ / 2; n.pitch = 40;
        if (at == PPQ) n.from = NoteFrom::Steps;
        edit::addNote (p, clip, n);
    }
    // A melody over held chords: top line and bottom line.
    for (int pitch : { 60, 64, 67 }) { Note n; n.start = PPQ * 3; n.length = PPQ; n.pitch = pitch; edit::addNote (p, clip, n); }
    Note high; high.start = PPQ * 3 + PPQ / 2; high.length = PPQ / 2; high.pitch = 72;
    edit::addNote (p, clip, high);
    const auto& c = *p.clip (clip);
    auto starts = [&] (const std::set<Id>& ids) { std::multiset<Tick> s; for (auto& n : c.midi.notes) if (ids.count (n.id)) s.insert (n.start); return s; };
    CHECK (starts (choose (c, p.tempo, What::OnBeat)) == std::multiset<Tick> ({ 0, PPQ, PPQ * 2 + 5, PPQ * 3, PPQ * 3, PPQ * 3 }));
    CHECK (starts (choose (c, p.tempo, What::OffBeat)) == std::multiset<Tick> ({ PPQ / 2, PPQ * 3 / 2, PPQ * 3 + PPQ / 2 }));
    CHECK (starts (choose (c, p.tempo, What::OnBeat, NoteFrom::Steps)) == std::multiset<Tick> ({ PPQ }));
    CHECK (pitches (c, choose (c, p.tempo, What::Top)) == std::multiset<int> ({ 40, 40, 40, 40, 40, 67, 72 }));
    CHECK (pitches (c, choose (c, p.tempo, What::Bottom)) == std::multiset<int> ({ 40, 40, 40, 40, 40, 60 }));
}

TEST ("explode chords: each voice to a track of its own, made like the clip's, top voice first; one clip each")
{
    Id clip = 0;
    auto p = song (clip);
    const Id original = p.tracks.front().id;
    edit::addDevice (p, original, PluginRef { "Kalibre", "synth", "Kalibre Synth" }, DeviceRole::Instrument);
    const auto made = explode (p, clip);
    CHECK_EQ (made.size(), size_t (5));
    CHECK (p.clip (clip) == nullptr);                       // moved, not copied
    if (made.size() != 5) return;
    CHECK_EQ (p.track (made.front())->name, std::string ("Piano 5 (Top)"));
    CHECK_EQ (p.track (made.back())->name, std::string ("Piano 1 (Bottom)"));
    CHECK_EQ (p.track (made.back())->devices.size(), size_t (1));    // its plug-ins, as the clip's track
    CHECK_EQ (p.indexOf (made.front()), p.indexOf (original) + 1);
    auto notesOf = [&] (Id t) { std::multiset<int> s; for (auto& c : p.track (t)->clips()) for (auto& n : c.midi.notes) s.insert (n.pitch); return s; };
    CHECK (notesOf (made.back()) == std::multiset<int> ({ 48, 45, 45, 43 }));
    CHECK (notesOf (made.front()) == std::multiset<int> ({ 57 }));
    CHECK (notesOf (made[3]) == std::multiset<int> ({ 52, 48, 48, 47 }));
    // A clip of single notes has nothing to explode.
    const Id t = edit::addTrack (p, TrackKind::Track, "Line");
    const Id line = edit::addMidiClip (p, t, 0, PPQ * 4);
    Note n; n.pitch = 60; n.length = PPQ; edit::addNote (p, line, n);
    CHECK (explode (p, line).empty());
    CHECK (p.clip (line) != nullptr);
}
