#include "Check.h"
#include "Comp.h"
#include "Edit.h"

using namespace kal;

static Clip audioAt (Tick start, Tick length, double sourceStart = 0)
{
    Clip c;
    c.kind = Clip::Kind::Audio;
    c.start = start;
    c.length = length;
    c.audio.file = "/take.wav";
    c.audio.sourceStart = sourceStart;
    return c;
}

TEST ("comp: a new take plays where it covers; swiping chooses another take for a stretch")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Vocal");
    const Id a = comp::addTake (p, t, { audioAt (0, PPQ * 16, 0.0) });
    const Id b = comp::addTake (p, t, { audioAt (0, PPQ * 16, 10.0) });
    auto& v = p.track (t)->version();
    CHECK_EQ (v.takes.size(), size_t (2));
    CHECK_EQ (v.comp.size(), size_t (1));
    CHECK_EQ (v.comp[0].take, b);   // the newest take wins
    CHECK_EQ (v.clips.size(), size_t (1));

    comp::choose (p, t, a, PPQ * 4, PPQ * 8);
    CHECK_EQ (v.comp.size(), size_t (3));
    CHECK_EQ (v.clips.size(), size_t (3));
    // The middle clip is take A's audio from 2 seconds in (bar 2 at 120 BPM), faded at both joins.
    const auto& mid = v.clips[1];
    CHECK_EQ (mid.fromTake, a);
    CHECK_EQ (mid.start, PPQ * 4);
    CHECK_EQ (mid.offset, PPQ * 4);
    CHECK (mid.audio.fadeIn > 0 && mid.audio.fadeOut > 0);
    CHECK_EQ (v.clips[0].fromTake, b);
    CHECK (v.clips[0].audio.fadeIn == 0);

    // Choosing B again across the whole thing merges it back into one stretch.
    comp::choose (p, t, b, 0, PPQ * 16);
    CHECK_EQ (v.comp.size(), size_t (1));
    CHECK_EQ (v.clips.size(), size_t (1));
}

TEST ("comp: removing a take and flattening")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Gtr");
    edit::addMidiClip (p, t, PPQ * 32, PPQ * 4);   // an ordinary clip, never touched by the comp
    const Id a = comp::addTake (p, t, { audioAt (0, PPQ * 8) });
    comp::addTake (p, t, { audioAt (0, PPQ * 4) });
    auto& v = p.track (t)->version();
    CHECK_EQ (v.clips.size(), size_t (3));   // B for 0-4 beats... then A after it, and the MIDI clip
    comp::removeTake (p, t, a);
    CHECK_EQ (v.clips.size(), size_t (2));
    comp::flatten (p, t);
    CHECK (v.takes.empty());
    for (auto& c : v.clips) CHECK_EQ (c.fromTake, Id (0));
}

TEST ("comp: a looped audio recording becomes a take per pass")
{
    TempoMap tempo;   // 120: a bar is 2 seconds
    Clip rec = audioAt (PPQ * 2, 1);    // started half a bar into a one-bar loop from bar 1
    const auto passes = comp::audioPasses (rec, 1.0 + 2.0 + 2.0 + 0.5, tempo, 0, PPQ * 4);
    CHECK_EQ (passes.size(), size_t (4));
    CHECK_EQ (passes[0].start, PPQ * 2);
    CHECK_EQ (passes[0].length, PPQ * 2);
    CHECK_EQ (passes[1].start, Tick (0));
    CHECK (std::abs (passes[1].audio.sourceStart - 1.0) < 1e-9);
    CHECK (std::abs (passes[2].audio.sourceStart - 3.0) < 1e-9);
    CHECK_EQ (passes[3].length, PPQ);   // the last half second
}

TEST ("comp: looped MIDI splits where the song goes back round")
{
    std::vector<capture::Event> ev;
    for (double t : { 100.0, 900.0, 3000.0, 50.0, 800.0, 20.0 })
    {
        capture::Event e;
        e.songTicks = t;
        ev.push_back (e);
    }
    const auto passes = comp::midiPasses (ev);
    CHECK_EQ (passes.size(), size_t (3));
    CHECK_EQ (passes[1].size(), size_t (2));
}

TEST ("comp: takes survive saving")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Vox");
    comp::addTake (p, t, { audioAt (0, PPQ * 4) });
    comp::addTake (p, t, { audioAt (0, PPQ * 4, 3.0) });
    p.track (t)->version().takesShown = true;
    // Saved and loaded through the project file.
    extern bool roundTrips (const Project&);
    CHECK (roundTrips (p));
}
