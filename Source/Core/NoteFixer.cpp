#include "NoteFixer.h"

#include <algorithm>
#include <functional>
#include <map>
#include <numeric>
#include <optional>
#include <set>

namespace kal::notefix
{
namespace
{
constexpr double pi = 3.14159265358979323846;

//==============================================================================
// Filters (engine.js: biquadRun, kWeight, bandpass, zeroPhase)

struct Biquad { double b0, b1, b2, a1, a2; };

Biquad norm (double b0, double b1, double b2, double a0, double a1, double a2)
{
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

void run (std::vector<double>& x, const Biquad& c)
{
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (auto& v : x)
    {
        const double xn = v;
        const double yn = c.b0 * xn + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
        x2 = x1; x1 = xn; y2 = y1; y1 = yn;
        v = yn;
    }
}

// ITU-R BS.1770 K-weighting, as loudness meters hear, at any rate.
std::vector<double> kWeight (const std::vector<float>& in, double fs)
{
    std::vector<double> x (in.begin(), in.end());
    double w0 = 2 * pi * 1681.974450955533 / fs;
    const double A = std::pow (10.0, 3.999843853973347 / 40);
    double alpha = std::sin (w0) / (2 * 0.7071752369554196);
    double cs = std::cos (w0), sa = 2 * std::sqrt (A) * alpha;
    run (x, norm (A * ((A + 1) + (A - 1) * cs + sa), -2 * A * ((A - 1) + (A + 1) * cs), A * ((A + 1) + (A - 1) * cs - sa),
                  (A + 1) - (A - 1) * cs + sa, 2 * ((A - 1) - (A + 1) * cs), (A + 1) - (A - 1) * cs - sa));
    w0 = 2 * pi * 38.13547087602444 / fs;
    alpha = std::sin (w0) / (2 * 0.5003270373238773);
    cs = std::cos (w0);
    run (x, norm ((1 + cs) / 2, -(1 + cs), (1 + cs) / 2, 1 + alpha, -2 * cs, 1 - alpha));
    return x;
}

Biquad bandpass (double fc, double q, double fs)
{
    const double w0 = 2 * pi * fc / fs, alpha = std::sin (w0) / (2 * q), cs = std::cos (w0);
    return norm (alpha, 0, -alpha, 1 + alpha, -2 * cs, 1 - alpha);
}

// Forward then backward: the filter adds no delay of its own.
void zeroPhase (std::vector<double>& x, const Biquad& c)
{
    run (x, c);
    std::reverse (x.begin(), x.end());
    run (x, c);
    std::reverse (x.begin(), x.end());
}

//==============================================================================
// Envelopes: mean power in a centred window every `hop` seconds.

struct Envelope
{
    std::vector<double> lin, db;
    double t0 = 0, hop = 0.0005;
    int size() const { return (int) db.size(); }
};

Envelope envelope (const std::vector<double>& sig, double fs, double hop, double win, long s0, long s1, bool squared)
{
    Envelope e;
    e.hop = hop;
    s0 = std::max (0L, s0);
    s1 = std::min ((long) sig.size(), s1);
    const long n = std::max (0L, (long) std::floor ((double) (s1 - s0) / (hop * fs)));
    const long half = std::max (1L, (long) std::lround (win * fs / 2));
    e.lin.assign ((size_t) n, 0.0);
    e.db.assign ((size_t) n, 0.0);
    auto sq = [&] (long i) { return squared ? sig[(size_t) i] : sig[(size_t) i] * sig[(size_t) i]; };
    long lo = std::max (0L, s0 - half), hi = lo;
    double sum = 0;
    for (long k = 0; k < n; ++k)
    {
        const long c = s0 + std::lround ((double) k * hop * fs);
        const long a = std::max (0L, c - half), b = std::min ((long) sig.size(), c + half);
        while (hi < b) sum += sq (hi++);
        while (lo < a) sum -= sq (lo++);
        const double v = std::max (0.0, sum) / (double) std::max (1L, b - a);
        e.lin[(size_t) k] = v;
        e.db[(size_t) k] = 10 * std::log10 (v + 1e-12);
    }
    e.t0 = (double) s0 / fs;
    return e;
}

//==============================================================================
// Statistics

double median (std::vector<double> a)
{
    if (a.empty()) return std::numeric_limits<double>::quiet_NaN();
    std::sort (a.begin(), a.end());
    const size_t m = a.size() / 2;
    return a.size() % 2 ? a[m] : (a[m - 1] + a[m]) / 2;
}

double quantile (std::vector<double> a, double q)
{
    if (a.empty()) return std::numeric_limits<double>::quiet_NaN();
    std::sort (a.begin(), a.end());
    const double i = std::clamp ((double) (a.size() - 1) * q, 0.0, (double) (a.size() - 1));
    const size_t lo = (size_t) std::floor (i);
    const double f = i - (double) lo;
    return lo + 1 < a.size() ? a[lo] * (1 - f) + a[lo + 1] * f : a[lo];
}

double stdev (const std::vector<double>& a)
{
    if (a.size() < 2) return 0;
    const double m = std::accumulate (a.begin(), a.end(), 0.0) / (double) a.size();
    double s = 0;
    for (double v : a) s += (v - m) * (v - m);
    return std::sqrt (s / (double) (a.size() - 1));
}

std::optional<std::vector<double>> solve (std::vector<std::vector<double>> A, std::vector<double> v)
{
    const size_t n = v.size();
    for (size_t i = 0; i < n; ++i) A[i].push_back (v[i]);
    for (size_t c = 0; c < n; ++c)
    {
        size_t piv = c;
        for (size_t r = c + 1; r < n; ++r) if (std::abs (A[r][c]) > std::abs (A[piv][c])) piv = r;
        std::swap (A[c], A[piv]);
        const double d = A[c][c];
        if (std::abs (d) < 1e-12) return std::nullopt;
        for (size_t r = 0; r < n; ++r)
        {
            if (r == c) continue;
            const double f = A[r][c] / d;
            for (size_t k = c; k <= n; ++k) A[r][k] -= f * A[c][k];
        }
    }
    std::vector<double> out (n);
    for (size_t i = 0; i < n; ++i) out[i] = A[i][n] / A[i][i];
    return out;
}

// Least squares that a few loud or quiet notes can't drag (Huber weights).
std::optional<std::vector<double>> robustFit (const std::vector<std::vector<double>>& X, const std::vector<double>& y, double& scale)
{
    const size_t n = y.size(), p = X[0].size();
    std::vector<double> w (n, 1.0);
    std::optional<std::vector<double>> beta;
    scale = 1;
    for (int it = 0; it < 30; ++it)
    {
        std::vector<std::vector<double>> M (p, std::vector<double> (p, 0.0));
        std::vector<double> v (p, 0.0);
        for (size_t i = 0; i < n; ++i)
            for (size_t a = 0; a < p; ++a)
            {
                v[a] += w[i] * X[i][a] * y[i];
                for (size_t b = 0; b < p; ++b) M[a][b] += w[i] * X[i][a] * X[i][b];
            }
        for (size_t a = 1; a < p; ++a) M[a][a] += 1e-6;
        auto nb = solve (M, v);
        if (! nb) break;
        beta = nb;
        std::vector<double> r (n), absr (n);
        for (size_t i = 0; i < n; ++i)
        {
            double f = 0;
            for (size_t a = 0; a < p; ++a) f += X[i][a] * (*beta)[a];
            r[i] = y[i] - f;
            absr[i] = std::abs (r[i]);
        }
        scale = std::max (0.3, 1.4826 * median (absr));
        const double k = 1.345 * scale;
        for (size_t i = 0; i < n; ++i) w[i] = std::abs (r[i]) <= k ? 1.0 : k / std::abs (r[i]);
    }
    return beta;
}

double hz (int pitch) { return 440.0 * std::pow (2.0, (pitch - 69) / 12.0); }

//==============================================================================
// Power in a note's harmonics, each band-passed without delay. Bands another
// note sounding with it also fills are left out where it has bands of its own.
std::vector<double> harmonicPower (const std::vector<float>& x, double fs, long s0, long s1, double f0, const std::vector<double>& others)
{
    s0 = std::max (0L, s0);
    s1 = std::min ((long) x.size(), s1);
    const size_t len = (size_t) std::max (0L, s1 - s0);
    std::vector<double> pow (len, 0.0);
    const bool narrow = ! others.empty();
    // Alone, a wide band reacts fastest; beside other notes, a narrow, steeper one keeps them out.
    const double bw = narrow ? std::max (f0 * 0.12, 8.0) : std::max (f0 * 0.5, 20.0);
    struct Band { double fc, gap; };
    std::vector<Band> bands;
    for (int h = 1; h <= 16; ++h)
    {
        const double fc = h * f0;
        if (fc > std::min (10000.0, fs * 0.45)) break;
        double gap = std::numeric_limits<double>::infinity();
        for (double g : others)
        {
            const double k = std::max (1.0, std::round (fc / g));
            gap = std::min ({ gap, std::abs (k * g - fc), std::abs ((k + 1) * g - fc), k > 1 ? std::abs ((k - 1) * g - fc) : gap });
        }
        bands.push_back ({ fc, gap });
    }
    std::vector<Band> use;
    if (narrow) { for (auto& b : bands) if (b.gap > 2.5 * bw) use.push_back (b); }
    else use = bands;
    if (use.empty() && ! bands.empty())
        use.push_back (*std::max_element (bands.begin(), bands.end(), [] (const Band& a, const Band& b) { return a.gap < b.gap; }));
    std::vector<double> seg (len);
    for (size_t b = 0; b < use.size() && b < 8; ++b)
    {
        for (size_t i = 0; i < len; ++i) seg[i] = x[(size_t) s0 + i];
        const auto c = bandpass (use[b].fc, use[b].fc / bw, fs);
        zeroPhase (seg, c);
        if (narrow) zeroPhase (seg, c);
        for (size_t i = 0; i < len; ++i) pow[i] += seg[i] * seg[i];
    }
    return pow;
}

struct Onset
{
    double onset = 0, attackTime = 0, startTime = 0, loudestTime = 0, peakDb = 0, riseDb = 0, slope = 0;
    bool atEdge = false;
};

// One note's onset inside [a, b] seconds of an envelope (engine.js findOnset).
std::optional<Onset> findOnset (const Envelope& env, double a, double b, double limit, const Options& o)
{
    const int n = env.size();
    if (n == 0) return std::nullopt;
    auto fr = [&] (double t) { return std::clamp ((int) std::lround ((t - env.t0) / env.hop), 0, n - 1); };
    const int ka = fr (a), kb = fr (b);
    if (kb <= ka) return std::nullopt;
    const int d = std::max (1, (int) std::lround (0.005 / env.hop));
    const auto& e = env.db;
    int ks = -1;
    double best = -1e300;
    for (int k = ka; k <= kb; ++k)
    {
        const double r = e[(size_t) std::min (n - 1, k + d)] - e[(size_t) std::max (0, k - d)];
        if (r > best) { best = r; ks = k; }
    }
    if (ks < 0) return std::nullopt;
    const int kLim = std::min (fr (limit), ks + (int) std::lround (0.15 / env.hop));
    int kp = ks;
    double pv = e[(size_t) ks];
    for (int k = ks; k <= kLim; ++k)
    {
        if (e[(size_t) k] > pv) { pv = e[(size_t) k]; kp = k; }
        else if (e[(size_t) k] < pv - 3) break;
    }
    const int kb0 = std::max (0, ks - (int) std::lround (0.06 / env.hop));
    double base = 1e300;
    for (int k = kb0; k <= ks; ++k) base = std::min (base, e[(size_t) k]);

    // Whatever was sounding before (the last note's tail, reverb, noise) is projected
    // forward as a steady decay and taken away, leaving this note's own sound.
    const int p0 = std::max (0, ks - (int) std::lround (0.06 / env.hop)), p1 = ks - (int) std::lround (0.015 / env.hop);
    double slope = 0, icept = base;
    if (p1 - p0 >= 8)
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0, m = 0;
        for (int k = p0; k <= p1; ++k) { sx += k; sy += e[(size_t) k]; sxx += (double) k * k; sxy += k * e[(size_t) k]; m++; }
        const double den = m * sxx - sx * sx;
        slope = den != 0 ? (m * sxy - sx * sy) / den : 0;
        icept = (sy - slope * sx) / m;
        if (slope > 0) { slope = 0; icept = sy / m; }
    }
    else if (p1 >= p0)
    {
        double sy = 0;
        for (int k = p0; k <= p1; ++k) sy += e[(size_t) k];
        icept = sy / (p1 - p0 + 1);
    }
    auto tail = [&] (int k) { return std::pow (10.0, (icept + slope * k) / 10); };
    std::vector<double> own ((size_t) (std::max (kp, kLim) - kb0 + 2), 0.0);
    double ownPeak = 0;
    int kOwnPeak = kp;
    for (int k = kb0; k <= std::max (kp, kb0); ++k)
    {
        const double v = std::max (0.0, env.lin[(size_t) k] - tail (k));
        own[(size_t) (k - kb0)] = v;
        if (k >= ks && v > ownPeak) { ownPeak = v; kOwnPeak = k; }
    }
    Onset r;
    r.slope = best;
    if (ownPeak <= 0)
    {
        r.onset = r.attackTime = r.startTime = r.loudestTime = env.t0 + ks * env.hop;
        r.peakDb = pv;
        r.riseDb = 0;
        r.atEdge = true;
        return r;
    }
    const double ownPeakDb = 10 * std::log10 (ownPeak);
    const double rise = ownPeakDb - (icept + slope * ks);
    // Where this note's own sound crosses `drop` dB below its peak, walking back from the
    // peak; never closer to what was already there than it can be told apart from it.
    auto crossing = [&] (double drop)
    {
        auto thrAt = [&] (int k) { return std::max (ownPeak * std::pow (10.0, -drop / 10), 0.5 * tail (k)); };
        int k = kOwnPeak;
        while (k > kb0 && own[(size_t) (k - 1 - kb0)] >= thrAt (k - 1)) --k;
        double t = env.t0 + k * env.hop;
        if (k > kb0)
        {
            const double a0 = own[(size_t) (k - 1 - kb0)] - thrAt (k - 1), a1 = own[(size_t) (k - kb0)] - thrAt (k);
            if (a1 > a0) t = env.t0 + ((k - 1) + (-a0) / (a1 - a0)) * env.hop;
        }
        return t;
    };
    r.attackTime = crossing (o.attackDb);
    r.startTime = crossing (o.startDb);
    r.onset = o.alignToAttack ? r.attackTime : r.startTime;
    r.loudestTime = env.t0 + kOwnPeak * env.hop;
    r.peakDb = ownPeakDb;
    r.riseDb = std::max (rise, ownPeakDb - base);
    r.atEdge = ks == ka || ks == kb;
    return r;
}

// The loudest 50 ms of [t0, t1], in dB.
double loudness (const Envelope& env, double t0, double t1)
{
    const int n = env.size();
    const int k0 = std::max (0, (int) std::lround ((t0 - env.t0) / env.hop));
    const int k1 = std::min (n, (int) std::lround ((t1 - env.t0) / env.hop));
    if (k1 - k0 < 1) return -std::numeric_limits<double>::infinity();
    const int L = std::min (k1 - k0, std::max (1, (int) std::lround (0.05 / env.hop)));
    double sum = 0;
    for (int k = k0; k < k0 + L; ++k) sum += env.lin[(size_t) k];
    double best = sum;
    for (int k = k0 + L; k < k1; ++k) { sum += env.lin[(size_t) k] - env.lin[(size_t) (k - L)]; best = std::max (best, sum); }
    return 10 * std::log10 (best / L + 1e-12);
}

struct Work
{
    const NoteIn* in = nullptr;
    size_t index = 0;
    double t = 0, tEnd = 0;
    int chord = -1;
    bool overlapped = false, under = false;
    double windowA = 0, windowB = 0, limit = 0;
    int next = -1;   // index into work
};
} // namespace

//==============================================================================
Analysis analyse (const std::vector<float>& x, double fs, const std::vector<NoteIn>& input, const Options& o)
{
    Analysis out;
    out.notes.assign (input.size(), {});
    if (input.empty() || x.empty() || fs <= 0) { out.warnings.push_back ("There are no notes to measure."); return out; }
    const double maxDelay = o.maxDelayMs / 1000, spread = o.spreadMs / 1000;
    const double duration = (double) x.size() / fs;

    std::vector<Work> notes (input.size());
    for (size_t i = 0; i < input.size(); ++i) notes[i] = { &input[i], i, input[i].seconds, input[i].endSeconds };
    std::vector<size_t> byStart (notes.size());
    std::iota (byStart.begin(), byStart.end(), 0);
    std::stable_sort (byStart.begin(), byStart.end(), [&] (size_t a, size_t b) { return notes[a].t < notes[b].t; });

    // Notes that sound together with another note.
    for (size_t i = 0; i < byStart.size(); ++i)
    {
        auto& a = notes[byStart[i]];
        for (size_t j = i + 1; j < byStart.size(); ++j)
        {
            auto& b = notes[byStart[j]];
            if (b.t - a.t >= 0.03 && b.t >= a.tEnd - 0.005) break;
            a.overlapped = b.overlapped = true;
        }
    }

    const auto kw = kWeight (x, fs);
    const auto benv = envelope (kw, fs, 0.0005, 0.003, 0, (long) kw.size(), false);

    // The part's typical delay: the lag at which the notes best line up with rises in the sound.
    {
        const int d = std::max (1, (int) std::lround (0.005 / benv.hop));
        const int nE = benv.size();
        std::vector<double> rise ((size_t) nE);
        for (int k = 0; k < nE; ++k) rise[(size_t) k] = std::max (0.0, benv.db[(size_t) std::min (nE - 1, k + d)] - benv.db[(size_t) std::max (0, k - d)]);
        double bestScore = -1;
        for (double L = -0.02; L <= maxDelay; L += benv.hop)
        {
            double s = 0;
            for (auto& n : notes)
            {
                const int k = (int) std::lround ((n.t + L - benv.t0) / benv.hop);
                if (k >= 1 && k < nE - 1) s += std::max ({ rise[(size_t) k - 1], rise[(size_t) k], rise[(size_t) k + 1] });
            }
            if (s > bestScore) { bestScore = s; out.typicalDelay = L; }
        }
    }
    const double typical = out.typicalDelay;

    // Chords: notes starting within 30 ms of each other, heard as one attack.
    int chordId = 0, chords = 0;
    for (size_t i = 0; i < byStart.size();)
    {
        size_t j = i + 1;
        while (j < byStart.size() && notes[byStart[j]].t - notes[byStart[i]].t < 0.03) ++j;
        for (size_t k = i; k < j; ++k) notes[byStart[k]].chord = j - i > 1 ? chordId : -1;
        if (j - i > 1) ++chords;
        ++chordId;
        i = j;
    }
    for (auto& n : notes) n.under = n.overlapped && n.chord < 0;

    // Where each note can be: bounded by its neighbours.
    for (auto& n : notes)
    {
        int prev = -1, next = -1;
        for (size_t m = 0; m < notes.size(); ++m)
        {
            const auto& w = notes[m];
            if (&w == &n || (n.chord >= 0 && w.chord == n.chord) || (n.under && w.in->pitch != n.in->pitch)) continue;
            if (w.t <= n.t && (prev < 0 || w.t > notes[(size_t) prev].t)) prev = (int) m;
            if (w.t > n.t && (next < 0 || w.t < notes[(size_t) next].t)) next = (int) m;
        }
        if (prev >= 0 && n.t - notes[(size_t) prev].t < 0.03) prev = -1;
        double a = std::max (n.t + typical - spread, n.t + std::min (0.0, typical) - 0.01);
        double b = std::min (n.t + typical + spread, n.t + maxDelay);
        if (prev >= 0) a = std::max (a, (notes[(size_t) prev].t + n.t) / 2 + typical);
        if (next >= 0) b = std::min (b, (n.t + notes[(size_t) next].t) / 2 + typical);
        n.limit = next >= 0 ? notes[(size_t) next].t + typical - 0.005 : n.t + maxDelay + 0.3;
        n.windowA = a;
        n.windowB = b;
        n.next = next;
    }

    // A note's own harmonics, band-passed out of the mix; `apart`: keeping out what sounds with it.
    std::map<std::pair<size_t, bool>, Envelope> harm;
    auto harmonicEnv = [&] (const Work& n, bool apart) -> const Envelope&
    {
        const auto key = std::make_pair (n.index, apart);
        if (auto it = harm.find (key); it != harm.end()) return it->second;
        const double f0 = hz (n.in->pitch);
        const long s0 = std::max (0L, (long) std::floor ((n.windowA - 0.12) * fs));
        const long s1 = std::min ((long) x.size(), (long) std::ceil ((std::max (n.limit, n.windowB) + 0.45) * fs) + 1);
        const long pad = std::lround (0.1 * fs);
        const long base = std::max (0L, s0 - pad);
        std::vector<double> others;
        if (apart)
            for (auto& m : notes)
                if (&m != &n && m.in->pitch != n.in->pitch && m.t < n.windowB && m.tEnd + 0.4 > n.windowA) others.push_back (hz (m.in->pitch));
        const auto pow = harmonicPower (x, fs, base, s1 + pad, f0, others);
        auto env = envelope (pow, fs, 0.0005, std::max (0.003, 0.5 / f0), s0 - base, std::min ((long) pow.size(), s1 - base), true);
        env.t0 += (double) base / fs;
        return harm.emplace (key, std::move (env)).first->second;
    };
    auto ok = [&] (const std::optional<Onset>& r) { return r && r->riseDb >= o.minRiseDb && r->slope >= 1; };

    std::map<int, std::optional<Onset>> chordResult;
    auto measure = [&] (const Work& n, bool whole) -> std::pair<std::optional<Onset>, std::string>
    {
        if (n.t >= duration || n.windowB - n.windowA < 0.004) return { std::nullopt, "whole sound" };
        if (n.chord >= 0)
        {
            if (! chordResult.count (n.chord)) chordResult[n.chord] = findOnset (benv, n.windowA, n.windowB, n.limit, o);
            return { chordResult[n.chord], "chord" };
        }
        if (whole) return { findOnset (benv, n.windowA, n.windowB, n.limit, o), "whole sound" };
        return { findOnset (harmonicEnv (n, n.under), n.windowA, n.windowB, n.limit, o), "harmonics" };
    };
    std::vector<double> harmonicShare (notes.size(), std::numeric_limits<double>::quiet_NaN());
    auto keep = [&] (const Work& n, const std::pair<std::optional<Onset>, std::string>& m)
    {
        auto& r = out.notes[n.index];
        r.measuredOn = m.second;
        r.found = ok (m.first);
        if (! r.found) return;
        r.onset = m.first->onset;
        r.delay = m.first->onset - n.t;
        r.check = m.first->atEdge;
        if (m.second == "harmonics")
        {
            const int k = std::clamp ((int) std::lround ((m.first->loudestTime - benv.t0) / benv.hop), 0, benv.size() - 1);
            harmonicShare[n.index] = m.first->peakDb - benv.db[(size_t) k];
        }
    };
    const bool wholeFirst = o.method == Options::Method::Whole;
    for (auto& n : notes) keep (n, measure (n, wholeFirst));
    if (o.method == Options::Method::Auto)
    {
        // Mostly not found on harmonics: an unpitched sound, or pitches the notes don't
        // match. The whole sound is used.
        std::vector<const Work*> single;
        for (auto& n : notes) if (n.chord < 0) single.push_back (&n);
        int onHarm = 0;
        std::vector<double> shares;
        for (auto* n : single) if (out.notes[n->index].found) { ++onHarm; if (! std::isnan (harmonicShare[n->index])) shares.push_back (harmonicShare[n->index]); }
        const double share = median (shares);
        if (! single.empty() && (onHarm < (int) single.size() / 2 || share < -12))
        {
            std::vector<std::pair<std::optional<Onset>, std::string>> tries;
            int okCount = 0;
            for (auto* n : single) { tries.push_back (measure (*n, true)); if (ok (tries.back().first)) ++okCount; }
            if (share < -12 || okCount > onHarm)
                for (size_t i = 0; i < single.size(); ++i) keep (*single[i], tries[i]);
        }
    }

    // Loudness: from the note's onset to its end, the next note's onset, or 400 ms.
    bool allWhole = true;
    for (auto& r : out.notes) if (r.found && r.measuredOn != "whole sound") allWhole = false;
    for (auto& n : notes)
    {
        auto& r = out.notes[n.index];
        r.chord = n.chord;
        r.overlapped = n.overlapped;
        r.under = n.under;
        if (! r.found) continue;
        double end = r.onset + std::min (0.4, std::max (0.05, n.tEnd - n.t));
        if (n.next >= 0 && out.notes[notes[(size_t) n.next].index].found) end = std::min (end, out.notes[notes[(size_t) n.next].index].onset);
        else if (n.next >= 0) end = std::min (end, notes[(size_t) n.next].t + typical);
        end = std::max (end, r.onset + 0.01);
        r.loudDb = loudness (allWhole ? benv : harmonicEnv (n, false), r.onset, end);
    }
    harm.clear();

    // Worth a listen: far from the other notes on the same key.
    for (auto& n : notes)
    {
        auto& r = out.notes[n.index];
        if (! r.found) continue;
        std::vector<double> same;
        for (auto& m : notes) if (&m != &n && m.in->pitch == n.in->pitch && out.notes[m.index].found) same.push_back (out.notes[m.index].delay);
        const bool far = same.size() >= 2 && std::abs (r.delay - median (same)) > 0.015;
        r.check = r.check || n.under || far;
    }

    // What each note's loudness should be, from its velocity, CC1, pitch and articulation.
    std::vector<const Work*> found;
    for (auto& n : notes) if (out.notes[n.index].found && std::isfinite (out.notes[n.index].loudDb)) found.push_back (&n);
    out.found = (int) found.size();
    if (found.size() >= 6)
    {
        auto vary = [&] (auto f)
        {
            std::vector<double> v;
            for (auto* n : found) { const double z = f (*n); if (z < 0) return false; v.push_back (z); }
            return stdev (v) > 2;
        };
        int lowP = 127, highP = 0;
        for (auto* n : found) { lowP = std::min (lowP, n->in->pitch); highP = std::max (highP, n->in->pitch); }
        std::vector<std::function<double (const Work&)>> terms;
        terms.push_back ([] (const Work&) { return 1.0; });
        int velocityTerm = -1;
        if (vary ([] (const Work& n) { return (double) n.in->velocity; })) { velocityTerm = (int) terms.size(); terms.push_back ([] (const Work& n) { return (double) n.in->velocity; }); }
        if (vary ([] (const Work& n) { return (double) n.in->cc1; })) terms.push_back ([] (const Work& n) { return (double) n.in->cc1; });
        if (highP - lowP >= 3) terms.push_back ([] (const Work& n) { return (n.in->pitch - 60) / 12.0; });
        if (highP - lowP >= 12 && found.size() >= 12) terms.push_back ([] (const Work& n) { return std::pow ((n.in->pitch - 60) / 12.0, 2); });
        // Each articulation is its own level: compared within it.
        std::set<int> arts;
        for (auto* n : found) arts.insert (n->in->articulation);
        for (auto it = std::next (arts.begin()); it != arts.end(); ++it)
        {
            const int a = *it;
            terms.push_back ([a] (const Work& n) { return n.in->articulation == a ? 1.0 : 0.0; });
        }
        std::vector<std::vector<double>> X;
        std::vector<double> y;
        for (auto* n : found)
        {
            std::vector<double> row;
            for (auto& t : terms) row.push_back (t (*n));
            X.push_back (row);
            y.push_back (out.notes[n->index].loudDb);
        }
        double scale = 1;
        if (auto beta = robustFit (X, y, scale))
        {
            out.fitted = true;
            if (velocityTerm >= 0 && (*beta)[(size_t) velocityTerm] >= 0.05 && (*beta)[(size_t) velocityTerm] <= 1.5)
                out.dbPerVelocity = (*beta)[(size_t) velocityTerm];
            for (size_t i = 0; i < found.size(); ++i)
            {
                double expected = 0;
                for (size_t a = 0; a < beta->size(); ++a) expected += X[i][a] * (*beta)[a];
                out.notes[found[i]->index].deviationDb = y[i] - expected;
            }
        }
    }
    else out.warnings.push_back ("Fewer than six notes were found, too few to judge which are too loud or too quiet.");

    std::vector<double> delays, devs;
    for (auto& r : out.notes)
    {
        if (r.found) delays.push_back (r.delay * 1000);
        if (std::isfinite (r.deviationDb)) devs.push_back (r.deviationDb);
    }
    out.medianDelayMs = delays.empty() ? 0 : median (delays);
    out.spreadMs = delays.empty() ? 0 : quantile (delays, 0.95) - quantile (delays, 0.05);
    out.loudnessSpreadDb = stdev (devs);
    const int notFound = (int) input.size() - (int) delays.size();
    if (notFound > 0)
        out.warnings.push_back (std::to_string (notFound) + (notFound == 1 ? " note wasn't" : " notes weren't") + " found in the render and won't be changed.");
    (void) chords;
    return out;
}

//==============================================================================
std::vector<NoteFix> plan (const Analysis& a, const std::vector<NoteIn>& notes, const PlanOptions& o)
{
    std::vector<NoteFix> out (notes.size());
    auto keyMedian = [&] (size_t i, bool delay)
    {
        std::vector<double> g;
        for (size_t j = 0; j < notes.size(); ++j)
        {
            const auto& m = a.notes[j];
            if (! m.found || notes[j].pitch != notes[i].pitch) continue;
            if (delay && std::abs (notes[j].velocity - notes[i].velocity) > 16) continue;
            const double v = delay ? m.delay : m.deviationDb;
            if (std::isfinite (v)) g.push_back (v);
        }
        return g.empty() ? (delay ? a.notes[i].delay : a.notes[i].deviationDb) : median (g);
    };
    std::vector<double> all;
    for (auto& m : a.notes) if (m.found) all.push_back (m.delay);
    const double partDelay = all.empty() ? 0 : median (all);
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& m = a.notes[i];
        auto& f = out[i];
        if (! m.found) continue;
        if (o.volume != PlanOptions::Volume::Off && std::isfinite (m.deviationDb))
        {
            const double dev = o.volume == PlanOptions::Volume::EachKey ? keyMedian (i, false) : m.deviationDb;
            if (std::isfinite (dev) && std::abs (dev) >= o.toleranceDb)
            {
                f.levelled = true;
                f.gainDb = -dev * o.volumeStrength;
            }
        }
        if (o.timing != PlanOptions::Timing::Off)
        {
            const double delay = o.timing == PlanOptions::Timing::EachKey ? keyMedian (i, true)
                               : o.timing == PlanOptions::Timing::WholePart ? partDelay : m.delay;
            const double shift = -(delay - o.targetMs / 1000) * o.timingStrength;
            if (std::abs (shift) >= 0.0005) { f.timed = true; f.shiftMs = shift * 1000; }
        }
    }
    // A chord's notes move together (their attack is one).
    std::map<int, std::vector<size_t>> chords;
    for (size_t i = 0; i < notes.size(); ++i) if (a.notes[i].found && a.notes[i].chord >= 0) chords[a.notes[i].chord].push_back (i);
    for (auto& [id, members] : chords)
    {
        std::vector<double> s;
        for (auto i : members) s.push_back (out[i].shiftMs);
        const double m = median (s);
        for (auto i : members) { out[i].shiftMs = m; out[i].timed = std::abs (m) >= 0.5; }
    }
    return out;
}

int ccForDb (double db)
{
    return std::clamp ((int) std::lround (127.0 * std::pow (10.0, db / 40.0)), 0, 127);
}

} // namespace kal::notefix
