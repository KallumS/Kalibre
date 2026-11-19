#include "WorkClock.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kal
{

void WorkClock::begin (double seconds, double tp)
{
    active = std::max (0.0, seconds);
    now = tp;
    lastInput = tp;
    idle = false;
}

void WorkClock::setAwayMinutes (int minutes)
{
    awayAfter = 60.0 * std::clamp (minutes, 1, 15);
}

void WorkClock::tick (double tp, bool input, bool playing, bool recording)
{
    // As G.tick: the step since the last call, never negative and never more than a second.
    const double dt = std::isnan (now) ? 0.0 : std::clamp (tp - now, 0.0, longestStep);
    now = tp;
    if (input || recording) lastInput = tp;
    const double limit = playing ? std::max (awayAfter, idlePlaying) : awayAfter;
    idle = (tp - lastInput) > limit;
    if (idle || dt <= 0) return;
    active += dt;
}

std::string WorkClock::format (double seconds)
{
    const long s = (long) std::floor (std::max (0.0, seconds));
    const long h = s / 3600, m = s % 3600 / 60, sec = s % 60;
    char buf[32];
    if (h > 0) std::snprintf (buf, sizeof buf, "%ld:%02ld:%02ld", h, m, sec);
    else std::snprintf (buf, sizeof buf, "%ld:%02ld", m, sec);
    return buf;
}

} // namespace kal
