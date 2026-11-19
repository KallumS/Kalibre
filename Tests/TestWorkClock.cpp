// The top bar's timer (decision 0091): DAW Battle Pass's active-time clock.
#include "Check.h"
#include "WorkClock.h"

#include <cmath>

using namespace kal;

namespace
{
// `seconds` of ticks four times a second, with input on every tick or none.
void run (WorkClock& w, double& tp, double seconds, bool input, bool playing = false, bool recording = false)
{
    for (double t = 0; t < seconds; t += 0.25) { tp += 0.25; w.tick (tp, input, playing, recording); }
}
} // namespace

TEST ("work clock: it runs while something happens, and goes on 3 minutes after, then stops")
{
    WorkClock w;
    double tp = 1000;
    w.begin (0, tp);
    run (w, tp, 60, true);
    CHECK (std::abs (w.active - 60) < 0.01);
    // Walked away: the next 3 minutes still count, then nothing.
    run (w, tp, 600, false);
    CHECK (w.idle);
    CHECK (std::abs (w.active - (60 + 180)) < 0.5);
    // Back: it runs again from the next thing, the time away not counted.
    run (w, tp, 30, true);
    CHECK (! w.idle);
    CHECK (std::abs (w.active - (60 + 180 + 30)) < 0.5);
}

TEST ("work clock: listening back counts for 8 minutes; recording always counts")
{
    WorkClock w;
    double tp = 0;
    w.begin (0, tp);
    run (w, tp, 600, false, true);           // playing, hands off
    CHECK (std::abs (w.active - 480) < 0.5);
    WorkClock r;
    tp = 0;
    r.begin (0, tp);
    run (r, tp, 1200, false, true, true);    // recording a long take
    CHECK (std::abs (r.active - 1200) < 0.5);
    CHECK (! r.idle);
}

TEST ("work clock: a stall counts a second at most; a song's time carries on from where it was")
{
    WorkClock w;
    double tp = 0;
    w.begin (3600, tp);
    w.tick (1, true, false, false);
    w.tick (500, true, false, false);        // asleep for eight minutes, then a key
    CHECK (std::abs (w.active - 3602) < 0.01);
    w.tick (400, true, false, false);        // a clock that went backwards counts nothing
    CHECK (std::abs (w.active - 3602) < 0.01);
}

TEST ("work clock: shown as a stopwatch")
{
    CHECK_EQ (WorkClock::format (0), std::string ("0:00"));
    CHECK_EQ (WorkClock::format (42.9), std::string ("0:42"));
    CHECK_EQ (WorkClock::format (725), std::string ("12:05"));
    CHECK_EQ (WorkClock::format (3729), std::string ("1:02:09"));
    CHECK_EQ (WorkClock::format (-5), std::string ("0:00"));
}

TEST ("work clock: the minutes before away are the musician's, 1 to 15")
{
    WorkClock w;
    w.setAwayMinutes (1);
    double tp = 0;
    w.begin (0, tp);
    run (w, tp, 10, true);
    run (w, tp, 300, false);
    CHECK (w.idle);
    CHECK (std::abs (w.active - (10 + 60)) < 0.5);
    w.setAwayMinutes (99);
    CHECK_EQ (w.awayAfter, 900.0);
    w.setAwayMinutes (0);
    CHECK_EQ (w.awayAfter, 60.0);
    // Listening back still allows 8 minutes when away is shorter.
    w.setAwayMinutes (2);
    w.begin (0, tp);
    run (w, tp, 400, false, true);
    CHECK (! w.idle);
}
