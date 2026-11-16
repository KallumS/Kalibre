#include "Check.h"
#include "Capture.h"

using namespace kal;
using namespace kal::capture;

static Event ev (double wall, double song, int s, int d1, int d2)
{
    Event e;
    e.wallSeconds = wall;
    e.songTicks = song;
    e.bytes[0] = static_cast<std::uint8_t> (s);
    e.bytes[1] = static_cast<std::uint8_t> (d1);
    e.bytes[2] = static_cast<std::uint8_t> (d2);
    return e;
}

TEST ("capture: the last performance after a pause, played while stopped")
{
    std::vector<Event> all {
        ev (1.0, -1, 0x90, 50, 90), ev (1.5, -1, 0x80, 50, 0),        // an earlier idea
        ev (10.0, -1, 0x90, 60, 100), ev (10.5, -1, 0x80, 60, 0),
        ev (10.5, -1, 0x90, 64, 80), ev (11.0, -1, 0x80, 64, 0),
    };
    TempoMap t;   // 120: half a second a beat
    const auto r = lastPerformance (all, t, PPQ * 4);
    CHECK (! r.wasPlaying);
    CHECK_EQ (r.start, PPQ * 4);
    CHECK_EQ (r.notes.size(), size_t (2));
    CHECK_EQ (r.notes[0].start, Tick (0));
    CHECK_EQ (r.notes[0].length, PPQ);
    CHECK_EQ (r.notes[1].start, PPQ);
    CHECK_EQ (r.notes[1].velocity, 80);
}

TEST ("capture: played with the song running keeps its place in the song")
{
    std::vector<Event> all {
        ev (5.0, PPQ * 9.0, 0x90, 60, 100), ev (5.5, PPQ * 10.0, 0x80, 60, 0),
        ev (5.6, PPQ * 10.0, 0xB0, 64, 127),
    };
    TempoMap t;
    const auto r = lastPerformance (all, t, 0);
    CHECK (r.wasPlaying);
    CHECK_EQ (r.start, PPQ * 8);          // the bar it was played in
    CHECK_EQ (r.notes[0].start, PPQ);
    CHECK_EQ (r.events.size(), size_t (1));
}

TEST ("capture: a held note through a pause is not cut in two")
{
    std::vector<Event> all {
        ev (1.0, -1, 0x90, 48, 90), ev (2.0, -1, 0x90, 60, 90),
        ev (9.0, -1, 0x90, 64, 90), ev (9.5, -1, 0x80, 64, 0), ev (9.6, -1, 0x80, 60, 0), ev (9.7, -1, 0x80, 48, 0),
    };
    TempoMap t;
    const auto r = lastPerformance (all, t, 0);
    CHECK_EQ (r.notes.size(), size_t (3));
}

TEST ("capture: the ring keeps only the newest")
{
    Ring<4> ring;
    for (int i = 0; i < 6; ++i) ring.push (ev (i, -1, 0x90, 60 + i, 100));
    const auto s = ring.snapshot();
    CHECK_EQ (s.size(), size_t (4));
    CHECK_EQ (int (s.front().bytes[1]), 62);
}
