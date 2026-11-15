#include "Check.h"
#include "TempoMap.h"

#include <cmath>

using namespace kal;

static bool near (double a, double b, double eps = 1e-6) { return std::abs (a - b) < eps; }

TEST ("tempo: a steady 120 is half a second a beat")
{
    TempoMap t;
    CHECK (near (t.secondsAt (PPQ), 0.5));
    CHECK (near (t.secondsAt (PPQ * 8), 4.0));
    CHECK_EQ (t.tickAt (4.0), PPQ * 8);
}

TEST ("tempo: a step change takes effect at its tick")
{
    TempoMap t;
    t.setTempoAt (PPQ * 4, 60.0);
    CHECK (near (t.secondsAt (PPQ * 4), 2.0));
    CHECK (near (t.secondsAt (PPQ * 5), 3.0));
    CHECK_EQ (t.tickAt (3.0), PPQ * 5);
}

TEST ("tempo: a ramp from 90 to 120 takes less time than 90 throughout and more than 120")
{
    TempoMap t;
    t.setTempos ({ { 0, 90.0, true }, { PPQ * 16, 120.0, false } });
    const double s = t.secondsAt (PPQ * 16);
    CHECK (s < 16 * 60.0 / 90.0);
    CHECK (s > 16 * 60.0 / 120.0);
    // The integral of 60/bpm over a linear ramp.
    CHECK (near (s, 60.0 * 16 * std::log (120.0 / 90.0) / 30.0, 1e-6));
    CHECK (near (t.bpmAt (PPQ * 8), 105.0));
    // Round trip anywhere in the ramp.
    for (Tick k : { Tick (1), PPQ, PPQ * 7 + 13, PPQ * 15 })
        CHECK_EQ (t.tickAt (t.secondsAt (k)), k);
}

TEST ("tempo: the first point can't be removed and BPM is clamped")
{
    TempoMap t;
    t.removeTempoAt (0);
    CHECK_EQ (t.tempos().size(), size_t (1));
    t.setTempoAt (0, 5000);
    CHECK (near (t.bpmAt (0), TempoMap::maxBpm));
}

TEST ("meter: bars follow time-signature changes")
{
    TempoMap t;
    t.setMeterAt (2, 3, 4);   // bar 3 in 3/4
    t.setMeterAt (4, 7, 8);   // bar 5 in 7/8
    CHECK_EQ (t.barStart (1), PPQ * 4);
    CHECK_EQ (t.barStart (2), PPQ * 8);
    CHECK_EQ (t.barStart (3), PPQ * 11);
    CHECK_EQ (t.barStart (4), PPQ * 14);
    CHECK_EQ (t.barStart (5), PPQ * 14 + PPQ * 7 / 2);
    CHECK_EQ (t.barAt (PPQ * 11), 3);
    CHECK_EQ (t.barAt (PPQ * 11 - 1), 2);
    CHECK_EQ (t.barAt (PPQ * 14 + PPQ * 7 / 2), 5);
    CHECK_EQ (t.formatBarBeat (PPQ * 12 + 240), std::string ("4.2.240"));
}

TEST ("meter: invalid signatures are refused")
{
    TempoMap t;
    t.setMeterAt (1, 4, 3);
    t.setMeterAt (1, 0, 4);
    CHECK_EQ (t.meters().size(), size_t (1));
}

TEST ("time: minutes and timecode")
{
    CHECK_EQ (TempoMap::formatMinutes (62.345), std::string ("1:02.345"));
    CHECK_EQ (TempoMap::formatTimecode (62.5, 25), std::string ("00:01:02:12"));
}
