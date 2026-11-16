#include "Surround.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace kal::surround
{

int channels (SurroundFormat f)
{
    switch (f)
    {
        case SurroundFormat::Stereo:     return 2;
        case SurroundFormat::Quad:       return 4;
        case SurroundFormat::Surround51: return 6;
        case SurroundFormat::Surround71: return 8;
    }
    return 2;
}

const char* name (SurroundFormat f)
{
    switch (f)
    {
        case SurroundFormat::Stereo:     return "Stereo";
        case SurroundFormat::Quad:       return "Quad";
        case SurroundFormat::Surround51: return "5.1";
        case SurroundFormat::Surround71: return "7.1";
    }
    return "Stereo";
}

namespace
{
struct Speaker { int channel; double degrees; };

// Directions: 0 straight ahead, positive to the right.
std::vector<Speaker> ring (SurroundFormat f)
{
    switch (f)
    {
        case SurroundFormat::Stereo:     return { { 0, -30 }, { 1, 30 } };
        case SurroundFormat::Quad:       return { { 0, -45 }, { 1, 45 }, { 3, 135 }, { 2, -135 } };
        case SurroundFormat::Surround51: return { { 0, -30 }, { 2, 0 }, { 1, 30 }, { 5, 110 }, { 4, -110 } };
        case SurroundFormat::Surround71: return { { 0, -30 }, { 2, 0 }, { 1, 30 }, { 7, 90 }, { 5, 150 }, { 4, -150 }, { 6, -90 } };
    }
    return {};
}

double wrap (double d)
{
    while (d < 0) d += 360.0;
    while (d >= 360.0) d -= 360.0;
    return d;
}
} // namespace

std::array<float, 8> gains (SurroundFormat f, double pan, double frontBack)
{
    std::array<float, 8> g {};
    if (f == SurroundFormat::Stereo)
    {
        // The stereo pan law (-3 dB in the middle), as the fader has always done.
        const double angle = (std::clamp (pan, -1.0, 1.0) + 1.0) * 3.141592653589793 * 0.25;
        g[0] = (float) (std::cos (angle) * std::sqrt (2.0));
        g[1] = (float) (std::sin (angle) * std::sqrt (2.0));
        return g;
    }
    // The direction of (pan, frontBack): 0 ahead, 90 right, 180 behind.
    const double direction = wrap (std::atan2 (std::clamp (pan, -1.0, 1.0), std::clamp (frontBack, -1.0, 1.0)) * 180.0 / 3.141592653589793);
    auto speakers = ring (f);
    // The two either side of it, going round the ring in order.
    std::sort (speakers.begin(), speakers.end(), [] (const Speaker& a, const Speaker& b) { return wrap (a.degrees) < wrap (b.degrees); });
    for (size_t i = 0; i < speakers.size(); ++i)
    {
        const auto& a = speakers[i];
        const auto& b = speakers[(i + 1) % speakers.size()];
        const double from = wrap (a.degrees);
        double span = wrap (b.degrees) - from;
        if (span <= 0) span += 360.0;
        const double into = wrap (direction - from);
        if (into > span) continue;
        const double t = into / span * 3.141592653589793 * 0.5;
        g[(size_t) a.channel] = (float) std::cos (t);
        g[(size_t) b.channel] = (float) std::sin (t);
        break;
    }
    return g;
}

} // namespace kal::surround
