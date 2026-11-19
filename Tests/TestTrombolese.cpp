// Trombolese (decision 0093): the port of KallumS/Trombolese's voice, checked
// against what the Python voice renders from the same controls. Its own rule:
// compiling is not evidence, check against the Python sample by sample.
#include "Check.h"
#include "Trombolese.h"

#include <cmath>
#include <fstream>
#include <vector>

using namespace kal::trombolese;

namespace
{
std::vector<double> reference (const char* name)
{
    std::ifstream f (std::string (KALIBRE_SOURCE_DIR) + "/Tests/fixtures/trombolese/" + name + ".f64", std::ios::binary);
    std::vector<double> v;
    double x;
    while (f.read (reinterpret_cast<char*> (&x), sizeof x)) v.push_back (x);
    return v;
}

double rms (const std::vector<double>& v, size_t from, size_t to)
{
    double s = 0;
    for (size_t i = from; i < to; ++i) s += v[i] * v[i];
    return std::sqrt (s / (double) (to - from));
}

// The strongest period over the last part, by autocorrelation, in Hz.
double pitch (const std::vector<double>& v, double rate)
{
    const size_t n = 6000, from = v.size() - n;
    int best = 0;
    double bestScore = -1e300;
    for (int lag = (int) (rate / 1000); lag < (int) (rate / 40); ++lag)
    {
        double s = 0;
        for (size_t i = from; i + (size_t) lag < v.size(); ++i) s += v[i] * v[i + (size_t) lag];
        if (s > bestScore) { bestScore = s; best = lag; }
    }
    return rate / best;
}

std::vector<double> render (const char* name)
{
    const auto g = Geometry::forRate (48000);
    Voice voice (g, 48000);
    voice.reset();
    const int n = (int) (0.3 * 48000);
    std::vector<double> out ((size_t) n);
    const double held = g.targetF1;
    auto ramp = [] (double pos) { return std::min (pos / 0.1, 1.0); };
    const std::string which (name);
    voice.renderAutomated (out.data(), n, [&] (double pos)
    {
        Controls c;
        c.lipFrequency = held * 0.97;
        if (which == "steady") c.pressure = 4200.0 * ramp (pos);
        if (which == "morphed") { c.pressure = 4000.0 * ramp (pos); c.alpha = 0.6; c.beta = 0.3; c.slide = 0.2; }
        if (which == "sweep") { c.pressure = 4200.0 * ramp (pos); c.alpha = pos; }
        return c;
    });
    return out;
}
} // namespace

TEST ("trombolese: the voice plays what the Python voice plays, from the same controls")
{
    for (const char* name : { "steady", "morphed", "sweep" })
    {
        const auto want = reference (name);
        const auto got = render (name);
        CHECK_EQ (want.size(), got.size());
        if (want.size() != got.size() || want.empty()) continue;
        // The attack, sample for sample.
        const double peak = rms (want, 0, want.size()) * 4;
        double worst = 0;
        for (size_t i = 0; i < 2400; ++i) worst = std::max (worst, std::abs (want[i] - got[i]));
        std::printf ("    %s: the first 50 ms within %.3g of a %.3g peak; ", name, worst, peak);
        CHECK (worst < peak * 1e-6);
        // The whole note: as loud, at the same pitch.
        const double a = rms (want, 4800, want.size()), b = rms (got, 4800, got.size());
        const double pa = pitch (want, 48000), pb = pitch (got, 48000);
        std::printf ("level %.4f against %.4f, pitch %.2f Hz against %.2f\n", b, a, pb, pa);
        CHECK (std::abs (a - b) < 0.01 * a);
        CHECK (std::abs (1200.0 * std::log2 (pa / pb)) < 5.0);
    }
}

TEST ("trombolese: the third regime is held where the Python tuned it, the bore grows as it morphs to a cone")
{
    const auto g = Geometry::forRate (48000);
    CHECK (std::abs (g.targetF1 - 181.33) < 0.1);
    CHECK (g.boreLengthAt (1.0) > g.boreLengthAt (0.0));
    Voice cylinder (g, 48000), cone (g, 48000);
    std::vector<double> out (4800);
    Controls c;
    c.pressure = 4200;
    c.lipFrequency = g.targetF1 * 0.97;
    cylinder.render (out.data(), 64, c);
    c.alpha = 1;
    cone.render (out.data(), 64, c);
    CHECK (cone.sections() > cylinder.sections());
    // Silence in, silence out: no breath, no sound.
    Voice still (g, 48000);
    Controls none;
    none.lipFrequency = g.targetF1;
    still.render (out.data(), (int) out.size(), none);
    double loudest = 0;
    for (double x : out) loudest = std::max (loudest, std::abs (x));
    CHECK (loudest < 1e-9);
}

TEST ("trombolese: a note finds its partial and slide position, and sounds in tune, at the cylinder, halfway and the cone")
{
    const auto g = Geometry::forRate (48000);
    for (double alpha : { 0.0, 0.5, 1.0 })
        for (int note : { 45, 50, 55, 57, 60, 64, 67 })
        {
            int sounded = 0;
            const auto s = settingFor (note, alpha, &sounded);
            Voice v (g, 48000);
            Controls c;
            c.pressure = 4200;
            c.alpha = alpha;
            c.lipFrequency = 440.0 * std::pow (2.0, (sounded - 69) / 12.0) * s.lipRatio;
            c.slide = s.slide;
            std::vector<double> out (21600);
            v.render (out.data(), (int) out.size(), c);
            const double want = 440.0 * std::pow (2.0, (sounded - 69) / 12.0);
            const double cents = 1200.0 * std::log2 (pitch (out, 48000) / want);
            if (std::abs (cents) > 30)
                check::fail (__FILE__, __LINE__, "note " + std::to_string (note) + " at alpha " + std::to_string (alpha)
                                                 + " sounds " + std::to_string ((int) cents) + " cents off");
            CHECK (rms (out, 12000, out.size()) > 1e-3);
        }
    // A note too low for the bore plays an octave up.
    int sounded = 0;
    settingFor (20, 0.0, &sounded);
    CHECK (sounded >= lowestNote && (sounded - 20) % 12 == 0);
}
