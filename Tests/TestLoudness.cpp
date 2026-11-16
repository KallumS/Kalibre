#include "Check.h"
#include "Loudness.h"

#include <vector>

using namespace kal;

static std::vector<float> sine (double freq, double amplitude, double seconds, double rate, double phase = 0)
{
    std::vector<float> v ((size_t) (seconds * rate));
    for (size_t k = 0; k < v.size(); ++k) v[k] = (float) (amplitude * std::sin (2 * 3.141592653589793 * freq * (double) k / rate + phase));
    return v;
}

TEST ("loudness: a 997 Hz sine at -20 dBFS in both channels reads -20 LUFS")
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto s = sine (997, 0.1, 6.0, rate);
        loudness::Meter m;
        m.prepare (rate);
        m.process (s.data(), s.data(), (int) s.size());
        CHECK (std::abs (m.momentary() + 20.0) < 0.1);
        CHECK (std::abs (m.shortTerm() + 20.0) < 0.1);
        CHECK (std::abs (m.integrated() + 20.0) < 0.15);
        CHECK (m.range() < 0.5);
        CHECK (std::abs (m.correlation() - 1.0) < 1e-3);
    }
}

TEST ("loudness: quiet passages below the gates don't pull the integrated reading down")
{
    const double rate = 48000;
    auto loud = sine (997, 0.1, 10.0, rate);
    auto quiet = sine (997, 0.0001, 10.0, rate);   // -80 dBFS: under the absolute gate
    loud.insert (loud.end(), quiet.begin(), quiet.end());
    loudness::Meter m;
    m.prepare (rate);
    m.process (loud.data(), loud.data(), (int) loud.size());
    CHECK (std::abs (m.integrated() + 20.0) < 0.15);
}

TEST ("loudness: true peak finds the peak between samples")
{
    const double rate = 48000;
    // A quarter of the sample rate, 45 degrees out: every sample is at 0.707, the wave reaches 1.
    const auto s = sine (rate / 4, 1.0, 1.0, rate, 3.141592653589793 / 4);
    loudness::Meter m;
    m.prepare (rate);
    m.process (s.data(), s.data(), (int) s.size());
    CHECK (std::abs (m.truePeakDb()) < 0.5);
}

TEST ("loudness: correlation of opposite channels is -1; the range of a two-level piece")
{
    const double rate = 48000;
    auto l = sine (440, 0.3, 2.0, rate);
    std::vector<float> r (l.size());
    for (size_t k = 0; k < l.size(); ++k) r[k] = -l[k];
    loudness::Meter m;
    m.prepare (rate);
    m.process (l.data(), r.data(), (int) l.size());
    CHECK (m.correlation() < -0.99);

    auto a = sine (997, 0.1, 20.0, rate);      // -20
    auto b = sine (997, 0.0316, 20.0, rate);   // -30
    a.insert (a.end(), b.begin(), b.end());
    m.reset();
    m.process (a.data(), a.data(), (int) a.size());
    CHECK (m.range() > 8.0 && m.range() < 11.0);
}
