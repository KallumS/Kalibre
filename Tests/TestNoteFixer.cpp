// Note Position & Volume Fixer, built in (decision 0085): its own tests,
// ported from Note-Position-Volume-Fixer's tests/run_tests.js and synth.js.
// A made-up library with known faults: each note late by its own amount, its
// attack rising over a while, three keys too loud or too quiet. Each test
// measures a render, fixes it, renders the fix and checks it against the
// library's own numbers, not the tool's opinion of itself.
#include "Check.h"

#include <array>
#include "NoteFixer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

using namespace kal;

namespace
{
double hash (int n)
{
    std::uint32_t x = (std::uint32_t) n ^ 0x9e3779b9u;
    x *= 0x85ebca6bu;
    x ^= x >> 13;
    x *= 0xc2b2ae35u;
    x ^= x >> 16;
    return x / 4294967296.0;
}

const std::map<int, double> faults { { 64, 5.0 }, { 67, -4.0 }, { 71, 3.5 } };
double delayMs (int p, int v) { return 45 + 70 * hash (p * 7 + 1) + (127 - v) * 0.12; }
double attackMs (int p) { return 12 + 30 * hash (p * 13 + 5); }
double gainDb (int p, int v) { auto f = faults.find (p); return -34 + 0.25 * v - 0.08 * (p - 60) + (f != faults.end() ? f->second : 0); }
double expectedOnsetMs (int p, int v)
{
    const double tau = std::acos (1 - 2 * std::pow (10.0, -12.0 / 20)) / 3.14159265358979;
    return delayMs (p, v) + tau * attackMs (p);
}

struct N { double beat, beats; int pitch, vel; double shiftMs = 0, gain = 0; };
constexpr double fs = 48000;

// Renders a part at 120 BPM: each note late by the library's amount, a fix's shift and gain applied.
std::vector<float> render (const std::vector<N>& part)
{
    double end = 0;
    for (auto& n : part) end = std::max (end, (n.beat + n.beats) * 0.5);
    std::vector<float> out ((size_t) ((end + 1.5) * fs));
    int s = 1;
    for (auto& v : out) { s = (int) (((unsigned) s * 1103515245u + 12345u) & 0x7fffffff); v = (float) ((s / (double) 0x7fffffff - 0.5) * std::pow (10.0, -80.0 / 20) * 3.4); }
    for (auto& n : part)
    {
        const double f0 = 440 * std::pow (2.0, (n.pitch - 69) / 12.0);
        const double t0 = n.beat * 0.5 + n.shiftMs / 1000 + delayMs (n.pitch, n.vel) / 1000;
        const double t1 = (n.beat + n.beats) * 0.5 + n.shiftMs / 1000 + delayMs (n.pitch, n.vel) / 1000;
        const double A = attackMs (n.pitch) / 1000;
        const double amp = std::pow (10.0, (gainDb (n.pitch, n.vel) + n.gain) / 20);
        double ph[6];
        for (int h = 1; h <= 6; ++h) ph[h - 1] = hash (n.pitch * 31 + h) * 6.283;
        const auto i0 = (long) std::floor (t0 * fs), i1 = std::min ((long) out.size(), (long) std::ceil ((t1 + 0.6) * fs));
        for (long i = std::max (0L, i0); i < i1; ++i)
        {
            const double t = i / fs - t0;
            if (t < 0) continue;
            double env = t < A ? 0.5 * (1 - std::cos (3.14159265358979 * t / A)) : std::exp (-(t - A) * 0.6);
            const double tr = i / fs - t1;
            if (tr > 0) env *= std::exp (-tr * 12);
            double v = 0;
            for (int h = 1; h <= 6 && h * f0 <= fs * 0.45; ++h) v += std::sin (6.283185307 * h * f0 * t + ph[h - 1]) / h;
            out[(size_t) i] += (float) (amp * env * v * 0.5);
        }
    }
    return out;
}

std::vector<notefix::NoteIn> notesOf (const std::vector<N>& part)
{
    std::vector<notefix::NoteIn> in;
    for (auto& n : part) { notefix::NoteIn x; x.pitch = n.pitch; x.velocity = n.vel; x.seconds = n.beat * 0.5; x.endSeconds = (n.beat + n.beats) * 0.5; in.push_back (x); }
    return in;
}

double med (std::vector<double> v) { std::sort (v.begin(), v.end()); return v.empty() ? 0 : v[v.size() / 2]; }
double worst (const std::vector<double>& v) { return v.empty() ? 0 : *std::max_element (v.begin(), v.end()); }

const int line[] { 60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62, 60, 64, 67, 72, 67, 64 };
std::vector<N> eighths (int shift = 0)
{
    std::vector<N> p;
    for (int i = 0; i < 20; ++i) p.push_back ({ 4 + i * 0.5, 0.25, line[i] + shift, std::array<int, 3> { 70, 90, 110 }[(size_t) (i % 3)] });
    return p;
}

// After the fix, how far each found note's sound lands from its written place (ms), by the library's numbers.
std::vector<double> trueAfter (const std::vector<N>& part, const notefix::Analysis& a, const std::vector<notefix::NoteFix>& f)
{
    std::vector<double> out;
    for (size_t i = 0; i < part.size(); ++i)
        if (a.notes[i].found) out.push_back (std::abs (f[i].shiftMs + expectedOnsetMs (part[i].pitch, part[i].vel)));
    return out;
}
} // namespace

TEST ("note fixer: a staccato line, nearly every note found to within a few ms; fixed, it lands on the grid")
{
    const auto part = eighths();
    const auto in = notesOf (part);
    const auto a = notefix::analyse (render (part), fs, in);
    CHECK (a.found >= 19);
    std::vector<double> err;
    for (size_t i = 0; i < part.size(); ++i)
        if (a.notes[i].found) err.push_back (std::abs (a.notes[i].delay * 1000 - expectedOnsetMs (part[i].pitch, part[i].vel)));
    CHECK (med (err) <= 2);
    CHECK (worst (err) <= 10);
    const auto fix = notefix::plan (a, in);
    const auto t = trueAfter (part, a, fix);
    CHECK (med (t) <= 3);
    CHECK (worst (t) <= 12);
    // Rendered again with the notes moved: measured on the grid.
    auto fixed = part;
    for (size_t i = 0; i < fixed.size(); ++i) fixed[i].shiftMs = fix[i].shiftMs;
    const auto again = notefix::analyse (render (fixed), fs, in);
    CHECK (std::abs (again.medianDelayMs) <= 3);
}

TEST ("note fixer: the louder and quieter keys are flagged by about the right amount, and evened out")
{
    const auto part = eighths();
    const auto in = notesOf (part);
    const auto a = notefix::analyse (render (part), fs, in);
    for (auto [pitch, fault] : faults)
    {
        std::vector<double> d;
        for (size_t i = 0; i < part.size(); ++i) if (a.notes[i].found && part[i].pitch == pitch) d.push_back (a.notes[i].deviationDb);
        CHECK (! d.empty());
        if (std::abs (med (d) - fault) > 1.5) check::fail (__FILE__, __LINE__, "key " + std::to_string (pitch) + ": " + std::to_string (med (d)));
    }
    std::vector<double> clean;
    for (size_t i = 0; i < part.size(); ++i) if (a.notes[i].found && ! faults.count (part[i].pitch)) clean.push_back (std::abs (a.notes[i].deviationDb));
    CHECK (med (clean) < 1);
    notefix::PlanOptions o;
    o.timing = notefix::PlanOptions::Timing::Off;
    const auto fix = notefix::plan (a, in, o);
    auto fixed = part;
    int levelled = 0;
    for (size_t i = 0; i < fixed.size(); ++i) { fixed[i].gain = fix[i].gainDb; levelled += fix[i].levelled ? 1 : 0; }
    CHECK (levelled >= 6);
    const auto again = notefix::analyse (render (fixed), fs, in);
    CHECK (again.loudnessSpreadDb < a.loudnessSpreadDb / 3);
}

TEST ("note fixer: low and high registers, legato, repeated notes")
{
    for (int shift : { -24, 19 })
    {
        const auto part = eighths (shift);
        const auto a = notefix::analyse (render (part), fs, notesOf (part));
        CHECK (a.found >= 18);
        const auto t = trueAfter (part, a, notefix::plan (a, notesOf (part)));
        CHECK (med (t) <= 4 && worst (t) <= 20);
    }
    std::vector<N> legato;
    for (int i = 0; i < 20; ++i) legato.push_back ({ 4.0 + i, 1.05, line[i], std::array<int, 3> { 70, 90, 110 }[(size_t) (i % 3)] });
    auto a = notefix::analyse (render (legato), fs, notesOf (legato));
    CHECK (a.found >= 18);
    auto t = trueAfter (legato, a, notefix::plan (a, notesOf (legato)));
    CHECK (med (t) <= 8 && worst (t) <= 15);
    std::vector<N> repeated;
    for (int i = 0; i < 16; ++i) repeated.push_back ({ 4 + i * 0.5, 0.2, 62, i % 2 ? 100 : 60 });
    a = notefix::analyse (render (repeated), fs, notesOf (repeated));
    CHECK_EQ (a.found, 16);
    CHECK (worst (trueAfter (repeated, a, notefix::plan (a, notesOf (repeated)))) <= 3);
}

TEST ("note fixer: a chord is heard as one attack, and its notes move together")
{
    const int chords[6][3] { { 48, 52, 55 }, { 50, 53, 57 }, { 52, 55, 59 }, { 53, 57, 60 }, { 55, 59, 62 }, { 48, 52, 55 } };
    std::vector<N> part;
    for (int c = 0; c < 6; ++c) for (int p : chords[c]) part.push_back ({ 4.0 + c * 2, 1.5, p, 90 });
    const auto a = notefix::analyse (render (part), fs, notesOf (part));
    const auto fix = notefix::plan (a, notesOf (part));
    for (int c = 0; c < 6; ++c)
    {
        CHECK (a.notes[(size_t) c * 3].chord >= 0);
        CHECK_EQ (fix[(size_t) c * 3].shiftMs, fix[(size_t) c * 3 + 2].shiftMs);
        double first = 1e9;
        for (int k = 0; k < 3; ++k) first = std::min (first, fix[(size_t) c * 3 + k].shiftMs + expectedOnsetMs (part[(size_t) c * 3 + k].pitch, 90));
        CHECK (std::abs (first) <= 10);
    }
}

TEST ("note fixer: MIDI volume follows the General MIDI curve")
{
    CHECK_EQ (notefix::ccForDb (0), 127);
    CHECK_EQ (notefix::ccForDb (notefix::ccToDb (100)), 100);
    CHECK (std::abs (notefix::ccToDb (100) - (-4.15)) < 0.01);
    CHECK_EQ (notefix::ccForDb (notefix::ccToDb (100) - 6), 71);
}
