#include "Check.h"
#include "Edit.h"
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

TEST ("song: a launched slot takes over from the next bar, looping")
{
    Id a, b;
    auto p = twoTracks (a, b);
    Clip loop;
    loop.length = bar;
    loop.midi.notes.push_back ({ .id = 1, .start = 0, .length = PPQ, .pitch = 72 });
    song::setSlot (p, a, 0, loop);
    CHECK_EQ (song::sceneCount (p), 1);
    song::launch (p, a, 0, PPQ);                  // asked in bar 1: plays from bar 2
    CHECK_EQ (p.track (a)->launch.from, bar);
    const auto out = playback::compile (p, *p.track (a));
    const double from = p.tempo.secondsAt (bar);
    int before = 0, slotNotes = 0;
    for (auto& m : out.midi)
    {
        if ((m.bytes[0] & 0xF0) != 0x90) continue;
        if (m.seconds < from) ++before;
        else { CHECK_EQ ((int) m.bytes[1], 72); ++slotNotes; }
    }
    CHECK_EQ (before, 1);         // the arrangement's note in bar 1
    CHECK (slotNotes > 100);      // the slot, bar after bar
    song::backToArrangement (p);
    CHECK (! song::anyLaunched (p));
}

TEST ("song: a scene is captured from the arrangement")
{
    Id a, b;
    auto p = twoTracks (a, b);
    const int scene = song::captureScene (p, 0, bar, "Intro");
    CHECK_EQ (scene, 0);
    CHECK_EQ (p.scenes[0], std::string ("Intro"));
    CHECK_EQ (p.track (a)->slots[0].midi.notes.size(), (size_t) 1);
    CHECK_EQ (p.track (a)->slots[0].loopLength, bar);
    CHECK (p.track (b)->slots.empty());          // nothing in bar 1 on the bass
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
