#include "Check.h"
#include "Edit.h"
#include "Harmony.h"
#include "Playback.h"
#include "Song.h"

using namespace kal;

static constexpr Tick bar = PPQ * 4;

static Project twoTracks (Id& a, Id& b)
{
    Project p;
    a = edit::addTrack (p, TrackKind::Track, "Keys");
    b = edit::addTrack (p, TrackKind::Track, "Bass");
    const Id c1 = edit::addMidiClip (p, a, 0, bar * 2);           // bars 1-2
    edit::addNote (p, c1, { .start = 0, .length = PPQ, .pitch = 60 });
    edit::addMidiClip (p, a, bar * 3, bar);                        // bar 4
    edit::addMidiClip (p, b, bar, bar * 2);                        // bars 2-3
    return p;
}

TEST ("song: inserting bars splits what crosses and moves the rest")
{
    Id a, b;
    auto p = twoTracks (a, b);
    p.markers.push_back ({ p.newId(), bar * 3, "Drop" });
    song::insertTime (p, bar, bar * 2);
    const auto& keys = p.track (a)->clips();
    CHECK_EQ (keys.size(), (size_t) 3);
    CHECK_EQ (keys[0].start, (Tick) 0);
    CHECK_EQ (keys[0].length, bar);
    CHECK_EQ (keys[1].start, bar * 3);     // the right half of the split
    CHECK_EQ (keys[1].offset, bar);
    CHECK_EQ (keys[2].start, bar * 5);
    CHECK_EQ (p.track (b)->clips()[0].start, bar * 3);
    CHECK_EQ (p.markers[0].tick, bar * 5);
}

TEST ("song: removing bars closes the gap")
{
    Id a, b;
    auto p = twoTracks (a, b);
    song::removeTime (p, bar, bar * 3);
    const auto& keys = p.track (a)->clips();
    CHECK_EQ (keys.size(), (size_t) 2);
    CHECK_EQ (keys[0].length, bar);
    CHECK_EQ (keys[1].start, bar);         // bar 4 is now bar 2
    CHECK (p.track (b)->clips().empty());
}

TEST ("song: a section duplicates, moves and goes with everything in it")
{
    Id a, b;
    auto p = twoTracks (a, b);
    const Id verse = song::addSection (p, "Verse", 0, bar * 2);
    const Id chorus = song::addSection (p, "Chorus", bar * 2, bar * 4);
    const Id copy = song::duplicateSection (p, verse);
    CHECK (copy != 0);
    CHECK_EQ (p.sections.size(), (size_t) 3);
    CHECK_EQ (p.sections[1].name, std::string ("Verse"));
    CHECK_EQ (p.sections[1].start, bar * 2);
    CHECK_EQ (p.sections[2].start, bar * 4);   // the chorus moved on
    // The copy of the keys' first clip, with its note.
    bool found = false;
    for (auto& c : p.track (a)->clips())
        if (c.start == bar * 2 && c.length == bar * 2 && c.midi.notes.size() == 1) found = true;
    CHECK (found);

    // The chorus to the front.
    song::moveSection (p, chorus, verse);
    CHECK_EQ (p.sections[0].name, std::string ("Chorus"));
    CHECK_EQ (p.sections[0].start, (Tick) 0);
    CHECK_EQ (p.sections[1].start, bar * 2);
    CHECK_EQ (p.sections[2].end, bar * 6);

    song::removeSection (p, p.sections[0].id, true);
    CHECK_EQ (p.sections.size(), (size_t) 2);
    CHECK_EQ (p.sections[0].start, (Tick) 0);
}

TEST ("song: ripple delete pulls later clips back")
{
    Id a, b;
    auto p = twoTracks (a, b);
    const Id first = p.track (a)->clips()[0].id;
    song::rippleRemove (p, { first });
    CHECK_EQ (p.track (a)->clips().size(), (size_t) 1);
    CHECK_EQ (p.track (a)->clips()[0].start, bar);
    CHECK_EQ (p.track (b)->clips()[0].start, bar);   // other tracks stay
}

TEST ("song: an edit to a linked clip is made to its twin")
{
    Id a, b;
    auto p = twoTracks (a, b);
    const Id first = p.track (a)->clips()[0].id;
    const Id twin = song::duplicateLinked (p, first);
    const auto before = p;
    edit::addNote (p, first, { .start = PPQ, .length = PPQ, .pitch = 64 });
    song::propagateLinks (before, p);
    CHECK_EQ (p.clip (twin)->midi.notes.size(), (size_t) 2);
    CHECK_EQ (p.clip (twin)->start, p.clip (first)->end());   // only the content is shared
    song::unlink (p, twin);
    const auto again = p;
    edit::addNote (p, first, { .start = PPQ * 2, .length = PPQ, .pitch = 67 });
    song::propagateLinks (again, p);
    CHECK_EQ (p.clip (twin)->midi.notes.size(), (size_t) 2);
}

TEST ("song: grouped tracks move together, keeping their differences")
{
    Id a, b;
    auto p = twoTracks (a, b);
    const Id c = edit::addTrack (p, TrackKind::Track, "Alone");
    p.track (a)->group = p.track (b)->group = 1;
    p.track (b)->volumeDb = -6;
    const auto before = song::mixOf (p);
    p.track (a)->volumeDb = -3;
    p.track (a)->mute = true;
    song::propagateGroups (before, p);
    CHECK_EQ (p.track (b)->volumeDb, -9.0);
    CHECK (p.track (b)->mute);
    CHECK (! p.track (c)->mute);
}

TEST ("song: moving the Chorus from bar 65 to bar 49 takes its piano, guitar and strings; what was at 49 follows it")
{
    Project p;
    const Tick bar = p.tempo.barStart (1);
    std::vector<Id> tracks;
    for (const char* name : { "Piano", "Guitar", "Strings" }) tracks.push_back (edit::addTrack (p, TrackKind::Track, name));
    // The verse from bar 49 (index 48) for 16 bars, the chorus from bar 65 for 8.
    const Id verse = song::addSection (p, "Verse", bar * 48, bar * 64);
    const Id chorus = song::addSection (p, "Chorus", bar * 64, bar * 72);
    for (auto t : tracks)
    {
        edit::addMidiClip (p, t, bar * 48, bar * 16);   // verse music
        edit::addMidiClip (p, t, bar * 64, bar * 8);    // chorus music
    }
    song::moveSection (p, chorus, verse);
    for (auto t : tracks)
    {
        const auto& clips = p.track (t)->clips();
        CHECK_EQ (clips.size(), size_t (2));
        if (clips.size() != 2) continue;
        CHECK_EQ (clips[0].start, bar * 48);    // the chorus music now starts at bar 49
        CHECK_EQ (clips[0].length, bar * 8);
        CHECK_EQ (clips[1].start, bar * 56);    // the verse music after it
        CHECK_EQ (clips[1].length, bar * 16);
    }
    CHECK_EQ (p.sections[0].name, std::string ("Chorus"));
    CHECK_EQ (p.sections[0].start, bar * 48);
    CHECK_EQ (p.sections[1].start, bar * 56);
}

TEST ("song: a section dragged to any bar takes its music; what was there moves out of the way, earlier or later")
{
    Project p;
    const Tick bar = p.tempo.barStart (1);
    const Id piano = edit::addTrack (p, TrackKind::Track, "Piano");
    const Id verse = song::addSection (p, "Verse", bar * 48, bar * 64);
    const Id chorus = song::addSection (p, "Chorus", bar * 64, bar * 72);
    edit::addMidiClip (p, piano, bar * 48, bar * 16);
    edit::addMidiClip (p, piano, bar * 64, bar * 8);

    // Earlier, to bar 53 (index 52): inside the verse, which is cut there to make room.
    song::moveSectionTo (p, chorus, bar * 52);
    song::sortSections (p);
    const Section* c = nullptr;
    for (auto& s : p.sections) if (s.id == chorus) c = &s;
    CHECK (c != nullptr);
    if (c) { CHECK_EQ (c->start, bar * 52); CHECK_EQ (c->end, bar * 60); }
    Tick total = 0;
    for (auto& cl : p.track (piano)->clips()) total += cl.length;
    CHECK_EQ (total, bar * 24);   // nothing lost
    bool chorusMusicThere = false;
    for (auto& cl : p.track (piano)->clips()) if (cl.start == bar * 52 && cl.length == bar * 8) chorusMusicThere = true;
    CHECK (chorusMusicThere);

    // Later: the verse's first part (bars 49-52) to after the chorus.
    Project q;
    const Id keys = edit::addTrack (q, TrackKind::Track, "Keys");
    const Id v = song::addSection (q, "Verse", bar * 48, bar * 64);
    song::addSection (q, "Chorus", bar * 64, bar * 72);
    edit::addMidiClip (q, keys, bar * 48, bar * 16);
    edit::addMidiClip (q, keys, bar * 64, bar * 8);
    song::moveSectionTo (q, v, bar * 56);
    song::sortSections (q);
    CHECK_EQ (q.sections[0].name, std::string ("Chorus"));
    CHECK_EQ (q.sections[0].start, bar * 48);
    CHECK_EQ (q.sections[1].start, bar * 56);
    const auto& clips = q.track (keys)->clips();
    CHECK_EQ (clips.size(), size_t (2));
    if (clips.size() == 2) { CHECK_EQ (clips[0].start, bar * 48); CHECK_EQ (clips[0].length, bar * 8); CHECK_EQ (clips[1].start, bar * 56); CHECK_EQ (clips[1].length, bar * 16); }
    // Where it already is: nothing changes.
    const auto before = q;
    song::moveSectionTo (q, v, bar * 56);
    CHECK (q == before);
}

TEST ("song: the loop set to clips spans them; chords are shown only for clips they were detected in")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Keys");
    const Id a = edit::addMidiClip (p, t, PPQ * 4 * 48, PPQ * 4 * 4);   // 4 bars at bar 49
    const Id b = edit::addMidiClip (p, t, PPQ * 4 * 60, PPQ * 4 * 2);
    CHECK (song::loopToClips (p, { a }));
    CHECK_EQ (p.loop.start, Tick (PPQ * 4 * 48));
    CHECK_EQ (p.loop.end, Tick (PPQ * 4 * 52));
    CHECK (p.loop.on);
    CHECK (song::loopToClips (p, { a, b }));
    CHECK_EQ (p.loop.end, Tick (PPQ * 4 * 62));
    CHECK (! song::loopToClips (p, {}));

    for (int pitch : { 60, 64, 67 }) { Note n; n.start = 0; n.length = PPQ * 4; n.pitch = pitch; edit::addNote (p, a, n); }
    CHECK (harmony::chordsAlong (p, 0, p.endTick(), true).empty());
    CHECK (! harmony::chordsAlong (p, 0, p.endTick()).empty());
    song::detectChords (p, { a }, true);
    const auto spans = harmony::chordsAlong (p, 0, p.endTick(), true);
    CHECK (! spans.empty());
    song::detectChords (p, { a }, false);
    CHECK (harmony::chordsAlong (p, 0, p.endTick(), true).empty());
}

TEST ("song: an alias clip's duplicates are aliases too; an edit to one is made to all (0074)")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Keys");
    const Id a = edit::addMidiClip (p, t, 0, PPQ * 4);
    Note n; n.start = 0; n.length = PPQ; n.pitch = 60;
    edit::addNote (p, a, n);
    song::makeAlias (p, { a });
    CHECK (p.clip (a)->link != 0);
    const Id link = p.clip (a)->link;
    song::makeAlias (p, { a });
    CHECK_EQ (p.clip (a)->link, link);      // left as it was
    const Id b = edit::duplicateClip (p, a);
    const Id c2 = edit::duplicateClip (p, b);
    CHECK_EQ (p.clip (b)->link, link);
    CHECK_EQ (p.clip (c2)->link, link);
    const auto before = p;
    n.start = PPQ * 2; n.pitch = 67;
    edit::addNote (p, c2, n);
    song::propagateLinks (before, p);
    CHECK_EQ (p.clip (a)->midi.notes.size(), size_t (2));
    CHECK_EQ (p.clip (b)->midi.notes.size(), size_t (2));
    song::unlink (p, b);
    const auto before2 = p;
    edit::addNote (p, b, n);
    song::propagateLinks (before2, p);
    CHECK_EQ (p.clip (a)->midi.notes.size(), size_t (2));
}
