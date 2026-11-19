// Makes Source/Core/TromboleseNotes.inc (decision 0093): for each MIDI note, at
// three bore positions, the embouchure and slide the Trombolese voice needs to
// sound it, found by playing it, as a trombonist finds a partial and a position,
// and as Trombolese's own compensate_reed_morph measures (a scan, then the slide
// trimmed by one over length). And each note's level, to even them out.
//   g++ -O2 -std=c++20 -ISource/Core tools/TromboleseCalibrate.cpp Source/Core/Trombolese.cpp -o calibrate
//   ./calibrate > Source/Core/TromboleseNotes.inc
#include "Trombolese.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <vector>

using namespace kal::trombolese;

namespace
{
constexpr double rate = 48000, pressure = 4200;

// The pitch it settles on, by autocorrelation over the last 0.15 s; 0 if it barely speaks.
double sounded (const Geometry& g, double alpha, double lip, double slide, double* level)
{
    Voice v (g, rate);
    Controls c;
    c.pressure = pressure; c.alpha = alpha; c.lipFrequency = lip; c.slide = slide;
    std::vector<double> out ((size_t) (0.45 * rate));
    v.render (out.data(), (int) out.size(), c);
    const size_t n = (size_t) (0.15 * rate), from = out.size() - n;
    double e = 0;
    for (size_t i = from; i < out.size(); ++i) e += out[i] * out[i];
    *level = std::sqrt (e / (double) n);
    if (*level < 1e-3) return 0;
    int best = 0;
    double score = -1e300;
    for (int lag = (int) (rate / 1400); lag < (int) (rate / 28); ++lag)
    {
        double s = 0;
        for (size_t i = from; i + (size_t) lag < out.size(); ++i) s += out[i] * out[i + (size_t) lag];
        if (s > score) { score = s; best = lag; }
    }
    // Refined between lags by a parabola.
    auto r = [&] (int lag) { double s = 0; for (size_t i = from; i + (size_t) lag < out.size(); ++i) s += out[i] * out[i + (size_t) lag]; return s; };
    const double a = r (best - 1), b = score, c2 = r (best + 1);
    const double shift = (a - 2 * b + c2) != 0 ? 0.5 * (a - c2) / (a - 2 * b + c2) : 0;
    return rate / (best + shift);
}
} // namespace

// One bore position a run (so the three can run side by side): `calibrate 0.5`. Without
// one, the table's head and foot are written, for putting the rows between.
int main (int argc, char** argv)
{
    const auto g = Geometry::forRate (rate);
    const double rest = 0.72;   // the mouthpiece, bell and end correction, near enough
    if (argc < 2)
    {
        std::printf ("// Made by tools/TromboleseCalibrate.cpp. Do not edit; make again.\n");
        std::printf ("// { lip / note frequency, slide m, level, cents off } for MIDI notes 34 to 84, at alpha 0, 0.5, 1.\n");
        return 0;
    }
    const double alpha = std::atof (argv[1]);
    const double L = g.boreLengthAt (alpha) + rest;
    std::printf ("  {   // alpha %.2f\n", alpha);
    for (int m = 34; m <= 84; ++m)
    {
        const double f = 440.0 * std::pow (2.0, (m - 69) / 12.0);
        struct Try { double err, ratio, slide; };
        std::vector<Try> tries;
        for (double ratio : { 0.90, 0.94, 0.97, 1.00, 1.04 })
            for (double slide = -0.4; slide <= 1.61; slide += 0.2)
            {
                double level = 0;
                const double p = sounded (g, alpha, f * ratio, slide, &level);
                if (p > 0) tries.push_back ({ std::abs (1200 * std::log2 (p / f)), ratio, slide });
            }
        std::sort (tries.begin(), tries.end(), [] (const Try& a, const Try& b) { return a.err + 30 * std::abs (a.slide) < b.err + 30 * std::abs (b.slide); });
        double bestErr = 1e9, bestLip = 0.97, bestSlide = 0, bestLevel = 0;
        for (size_t t = 0; t < std::min<size_t> (3, tries.size()); ++t)
        {
            double s = tries[t].slide, level = 0;
            double p = sounded (g, alpha, f * tries[t].ratio, s, &level);
            // Trim the slide: within a regime, frequency goes as one over length.
            for (int k = 0; k < 2 && p > 0; ++k)
            {
                s = std::clamp ((L + s) * (p / f) - L, -0.6, 2.0);
                p = sounded (g, alpha, f * tries[t].ratio, s, &level);
            }
            if (p <= 0) continue;
            const double err = std::abs (1200 * std::log2 (p / f));
            if (err < bestErr) { bestErr = err; bestLip = tries[t].ratio; bestSlide = s; bestLevel = level; }
        }
        std::printf ("    { %.3f, %.5f, %.5f, %.1f },   // %d\n", bestLip, bestSlide, bestLevel, bestErr > 1e8 ? 9999.0 : bestErr, m);
        std::fflush (stdout);
    }
    std::printf ("  },\n");
}
