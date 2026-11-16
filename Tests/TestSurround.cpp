#include "Check.h"
#include "Surround.h"

#include <cmath>

using namespace kal;

static double power (const std::array<float, 8>& g)
{
    double p = 0;
    for (auto v : g) p += (double) v * v;
    return p;
}

TEST ("surround: a track ahead in 5.1 is in the centre speaker; behind-left in the left surround")
{
    const auto ahead = surround::gains (SurroundFormat::Surround51, 0.0, 1.0);
    CHECK (std::abs (ahead[2] - 1.0f) < 1e-5f);
    CHECK (ahead[3] == 0.0f);   // nothing to the LFE
    const auto behindLeft = surround::gains (SurroundFormat::Surround51, -0.94, -0.34);   // about -110 degrees
    CHECK (behindLeft[4] > 0.95f);
    for (double pan : { -1.0, -0.5, 0.0, 0.3, 1.0 })
        for (double fb : { -1.0, 0.0, 0.5, 1.0 })
            CHECK (std::abs (power (surround::gains (SurroundFormat::Surround71, pan, fb)) - 1.0) < 1e-5);
}

TEST ("surround: stereo keeps the -3 dB pan law; quad's front-right is between L and R... and the rear")
{
    const auto mid = surround::gains (SurroundFormat::Stereo, 0.0, 1.0);
    CHECK (std::abs (mid[0] - 1.0f) < 1e-5f && std::abs (mid[1] - 1.0f) < 1e-5f);
    const auto right = surround::gains (SurroundFormat::Quad, 1.0, 0.0);   // 90 degrees: half way from 45 to 135
    CHECK (std::abs (right[1] - right[3]) < 1e-5f);
    CHECK (right[0] == 0.0f && right[2] == 0.0f);
    CHECK_EQ (surround::channels (SurroundFormat::Surround71), 8);
}
