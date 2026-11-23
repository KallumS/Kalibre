#include "Harmony.h"

#include "ScaleModel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>

namespace kal::harmony
{

namespace
{
size_t pcIndex (int pc) { return static_cast<size_t> (((pc % 12) + 12) % 12); }

scaleview::Key keyOf (const ScaleChoice& s)
{
    const int root = std::clamp (s.root, 0, static_cast<int> (scaleview::roots.size()) - 1);
    const int scale = std::clamp (s.scale, 0, static_cast<int> (scaleview::scales.size()) - 1);
    return scaleview::buildKey (root, scale);
}

std::string lower (std::string s)
{
    for (auto& c : s) c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
    return s;
}
} // namespace

bool isDrumTrack (const Track& t)
{
    const auto n = lower (t.name);
    return n.find ("drum") != std::string::npos || n.find ("perc") != std::string::npos
        || n.find ("kit") != std::string::npos;
}

std::vector<TimedPitch> collect (const Project& p, Tick from, Tick to, bool detectedOnly)
{
    std::vector<TimedPitch> out;
    // Only a solo of a track with notes, or of a folder holding some, says
    // which are heard: soloing a return left the chord lane empty (the UAT).
    bool anySolo = false;
    for (auto& t : p.tracks) anySolo = anySolo || (t.solo && (t.holdsClips() || t.isFolder()));
    auto soloed = [&p] (const Track& t)
    {
        if (t.solo) return true;
        for (auto& f : p.tracks)
            if (f.solo && f.isFolder() && p.isInside (t.id, f.id)) return true;
        return false;
    };
    for (auto& t : p.tracks)
    {
        if (! t.holdsClips() || t.mute || (anySolo && ! soloed (t)) || isDrumTrack (t)) continue;
        for (auto& c : t.clips())
        {
            if (! c.isMidi() || c.muted || c.end() <= from || c.start >= to || (detectedOnly && ! c.detectChords)) continue;
            const Tick content = c.loopLength > 0 ? c.loopLength : std::numeric_limits<Tick>::max() / 4;
            for (Tick base = c.start - c.offset; base < c.end(); base += content)
            {
                for (auto& n : c.midi.notes)
                {
                    if (n.channel == 9 || n.from == NoteFrom::Drums || n.from == NoteFrom::Euclid) continue;   // drums, not harmony
                    if (c.loopLength > 0 && n.start >= c.loopLength) continue;
                    Tick a = base + n.start, b = a + n.length;
                    a = std::max (a, c.start);
                    b = std::min (b, c.end());
                    if (b <= a || b <= from || a >= to) continue;
                    out.push_back ({ n.pitch, a, b - a });
                }
                if (c.loopLength <= 0) break;
            }
        }
    }
    std::sort (out.begin(), out.end(), [] (const TimedPitch& a, const TimedPitch& b)
    {
        return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
    });
    return out;
}

std::vector<int> soundingAt (const Project& p, Tick t)
{
    std::vector<int> out;
    for (auto& n : collect (p, t, t + 1))
        if (n.start <= t && n.start + n.length > t) out.push_back (n.pitch);
    std::sort (out.begin(), out.end());
    out.erase (std::unique (out.begin(), out.end()), out.end());
    return out;
}

std::string chordName (const std::vector<int>& pitches, const ScaleChoice& s)
{
    std::vector<int> held (pitches);
    std::sort (held.begin(), held.end());
    held.erase (std::unique (held.begin(), held.end()), held.end());
    std::array<bool, 12> classes {};
    int count = 0;
    for (int p : held) if (! classes[pcIndex (p)]) { classes[pcIndex (p)] = true; ++count; }
    if (count < 2) return {};
    return scaleview::chordName (held, keyOf (s));
}

//==============================================================================
// The chord lane - Miderator's detector, ported.

namespace
{
struct Template { int root; int quality; };

const std::vector<std::vector<int>> qualities {
    { 0, 4, 7 }, { 0, 3, 7 }, { 0, 4, 7, 10 }, { 0, 3, 7, 10 }, { 0, 4, 7, 11 },
    { 0, 3, 6 }, { 0, 3, 6, 10 }, { 0, 3, 6, 9 }, { 0, 4, 8 }, { 0, 5, 7 }, { 0, 2, 7 },
};

struct Weights
{
    std::array<double, 12> w {};
    int bass = -1;
    double total = 0;
};

Weights weigh (const std::vector<TimedPitch>& notes, Tick from, Tick to)
{
    Weights out;
    int lowest = 200;
    for (const auto& n : notes)
    {
        const Tick a = std::max (from, n.start), b = std::min (to, n.start + n.length);
        if (b <= a) continue;
        const double w = static_cast<double> (b - a) / static_cast<double> (to - from);
        out.w[pcIndex (n.pitch)] += w;
        out.total += w;
        lowest = std::min (lowest, n.pitch);
    }
    out.bass = lowest < 200 ? lowest : -1;
    return out;
}

double fit (const Weights& w, Template t)
{
    double in = 0, out = 0;
    std::array<bool, 12> member {};
    for (int iv : qualities[static_cast<size_t> (t.quality)]) member[pcIndex (t.root + iv)] = true;
    int missing = 0;
    for (size_t pc = 0; pc < 12; ++pc)
    {
        if (member[pc]) { in += w.w[pc]; if (w.w[pc] <= 0) ++missing; }
        else out += w.w[pc];
    }
    double score = in - out - 0.35 * missing;
    if (w.bass >= 0 && pcIndex (w.bass) == pcIndex (t.root)) score += 0.15;
    return score;
}

Template best (const Weights& w)
{
    Template winner { 0, 0 };
    double top = -1e9;
    for (int q = 0; q < static_cast<int> (qualities.size()); ++q)
        for (int r = 0; r < 12; ++r)
        {
            const double s = fit (w, { r, q });
            if (s > top + 1e-9) { top = s; winner = { r, q }; }
        }
    return winner;
}
} // namespace

std::vector<ChordSpan> chordsAlong (const Project& p, Tick from, Tick to, bool detectedOnly)
{
    std::vector<ChordSpan> out;
    from = std::max<Tick> (0, from);
    if (to <= from) return out;
    const auto notes = collect (p, from, to, detectedOnly);
    if (notes.empty()) return out;

    std::vector<std::pair<Tick, Tick>> beats;
    for (int bar = p.tempo.barAt (from); p.tempo.barStart (bar) < to; ++bar)
    {
        const auto& m = p.tempo.meterAtBar (bar);
        const Tick start = p.tempo.barStart (bar), len = m.barTicks(), beat = m.beatTicks();
        for (Tick t = start; t < start + len; t += beat)
            if (t + beat > from && t < to) beats.emplace_back (std::max (t, from), std::min (t + beat, to));
    }

    struct Seg { Tick a, b; Template t; bool empty; };
    std::vector<Seg> segs;
    for (const auto& [a, b] : beats)
    {
        const auto w = weigh (notes, a, b);
        if (w.total <= 0)
        {
            if (! segs.empty() && ! segs.back().empty) segs.back().b = b;
            else segs.push_back ({ a, b, { 0, 0 }, true });
            continue;
        }
        const auto t = best (w);
        if (! segs.empty() && ! segs.back().empty)
        {
            auto& s = segs.back();
            const auto joined = best (weigh (notes, s.a, b));
            if (joined.root == s.t.root && joined.quality == s.t.quality
                && (t.root == s.t.root || fit (w, s.t) >= fit (w, t) - 0.25))
            {
                s.b = b;
                continue;
            }
        }
        segs.push_back ({ a, b, t, false });
    }

    for (const auto& s : segs)
    {
        if (s.empty) continue;
        const auto w = weigh (notes, s.a, s.b);
        std::array<bool, 12> member {};
        for (int iv : qualities[static_cast<size_t> (s.t.quality)]) member[pcIndex (s.t.root + iv)] = true;
        std::map<int, int> lowestOf;
        for (const auto& n : notes)
        {
            if (n.start + n.length <= s.a || n.start >= s.b) continue;
            const size_t pc = pcIndex (n.pitch);
            if (! member[pc] && w.w[pc] < 0.2) continue;
            auto it = lowestOf.find (static_cast<int> (pc));
            if (it == lowestOf.end() || n.pitch < it->second) lowestOf[static_cast<int> (pc)] = n.pitch;
        }
        int together = 0, most = 0;
        {
            std::vector<std::pair<Tick, int>> edges;
            for (const auto& n : notes)
            {
                const Tick a = std::max (s.a, n.start), b = std::min (s.b, n.start + n.length);
                if (b > a) { edges.push_back ({ a, 1 }); edges.push_back ({ b, -1 }); }
            }
            std::sort (edges.begin(), edges.end(), [] (const auto& x, const auto& y)
            {
                return x.first != y.first ? x.first < y.first : x.second < y.second;
            });
            for (const auto& e : edges) { together += e.second; most = std::max (most, together); }
        }
        ChordSpan span;
        span.start = s.a;
        span.end = s.b;
        for (const auto& kv : lowestOf) span.pitches.push_back (kv.second);
        if (w.bass >= 0 && std::find (span.pitches.begin(), span.pitches.end(), w.bass) == span.pitches.end())
        {
            const int bassPc = w.bass % 12;
            span.pitches.erase (std::remove_if (span.pitches.begin(), span.pitches.end(),
                                                [bassPc] (int x) { return x % 12 == bassPc; }),
                                span.pitches.end());
            span.pitches.push_back (w.bass);
        }
        std::sort (span.pitches.begin(), span.pitches.end());
        bool outlined = span.pitches.size() >= 3;
        for (int x : span.pitches) outlined = outlined && member[pcIndex (x)];
        const bool isHarmony = most >= 2 || outlined;
        span.name = (isHarmony && span.pitches.size() >= 2) ? chordName (span.pitches, p.scale) : std::string {};
        if (! out.empty() && out.back().name == span.name && out.back().end == span.start)
        {
            out.back().end = span.end;
            continue;
        }
        out.push_back (span);
    }
    return out;
}

//==============================================================================
// Keys - Midi Suggester's detectKey, ported with its weights.

std::vector<KeyGuess> rankKeys (const std::vector<TimedPitch>& notes)
{
    static constexpr std::array<double, 12> majorProfile { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static constexpr std::array<double, 12> minorProfile { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    static const std::array<const char*, 12> majorRoot { "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    static const std::array<const char*, 12> minorRoot { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "G#", "A", "Bb", "B" };

    auto rootIndexOf = [] (const char* name)
    {
        for (size_t i = 0; i < scaleview::roots.size(); ++i)
            if (std::string (scaleview::roots[i].name) == name) return static_cast<int> (i);
        return 0;
    };
    auto correlate = [] (const std::array<double, 12>& xs, const std::array<double, 12>& ys)
    {
        double mx = 0, my = 0;
        for (size_t i = 0; i < 12; ++i) { mx += xs[i]; my += ys[i]; }
        mx /= 12; my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (size_t i = 0; i < 12; ++i)
        {
            const double dx = xs[i] - mx, dy = ys[i] - my;
            sxy += dx * dy; sxx += dx * dx; syy += dy * dy;
        }
        if (sxx <= 0 || syy <= 0) return 0.0;
        return sxy / std::sqrt (sxx * syy);
    };

    std::array<double, 12> hist {};
    double total = 0;
    int firstPc = -1, lastPc = -1;
    if (! notes.empty())
    {
        Tick first = notes.front().start, last = 0;
        for (auto& n : notes) last = std::max (last, n.start);
        int lo = 200, loLast = 200;
        for (auto& n : notes)
        {
            if (n.start == first) lo = std::min (lo, n.pitch);
            if (n.start == last) loLast = std::min (loLast, n.pitch);
        }
        firstPc = lo % 12;
        lastPc = loLast % 12;
    }
    for (const auto& n : notes)
    {
        const double w = std::max (static_cast<double> (n.length) / PPQ, 0.25);
        hist[pcIndex (n.pitch)] += w;
        total += w;
    }
    if (total <= 0) total = 1;
    auto at = [&hist] (int pc) { return hist[pcIndex (pc)]; };

    std::vector<KeyGuess> ranked;
    for (int tonic = 0; tonic < 12; ++tonic)
        for (int mode = 0; mode < 2; ++mode)
        {
            const auto& profile = mode == 0 ? majorProfile : minorProfile;
            std::array<double, 12> rotated {};
            for (int i = 0; i < 12; ++i) rotated[static_cast<size_t> (i)] = profile[pcIndex (i - tonic)];
            double score = correlate (hist, rotated);
            std::array<bool, 12> inKey {};
            for (int iv : scaleview::scales[static_cast<size_t> (mode)].intervals) inKey[pcIndex (tonic + iv)] = true;
            if (mode == 1) inKey[pcIndex (tonic + 11)] = true;
            double outside = 0;
            for (int pc = 0; pc < 12; ++pc) if (! inKey[static_cast<size_t> (pc)]) outside += at (pc);
            const int third = mode == 0 ? 4 : 3;
            const double triad = at (tonic) + at (tonic + third) + at (tonic + 7);
            score = score - 1.0 * outside / total + 0.6 * triad / total;
            if (lastPc == tonic) score += 0.2;
            if (firstPc == tonic) score += 0.05;
            ranked.push_back ({ rootIndexOf ((mode == 0 ? majorRoot : minorRoot)[static_cast<size_t> (tonic)]), mode, score });
        }
    std::stable_sort (ranked.begin(), ranked.end(), [] (const KeyGuess& a, const KeyGuess& b) { return a.score > b.score; });
    return ranked;
}

//==============================================================================
bool inScale (const ScaleChoice& s, int pitch)
{
    const int root = scaleview::roots[static_cast<size_t> (std::clamp (s.root, 0, static_cast<int> (scaleview::roots.size()) - 1))].pitchClass();
    const auto& scale = scaleview::scales[static_cast<size_t> (std::clamp (s.scale, 0, static_cast<int> (scaleview::scales.size()) - 1))];
    for (int iv : scale.intervals)
        if (pcIndex (root + iv) == pcIndex (pitch)) return true;
    return false;
}

bool isRoot (const ScaleChoice& s, int pitch)
{
    const int root = scaleview::roots[static_cast<size_t> (std::clamp (s.root, 0, static_cast<int> (scaleview::roots.size()) - 1))].pitchClass();
    return pcIndex (root) == pcIndex (pitch);
}

std::string scaleName (const ScaleChoice& s)
{
    const auto& root = scaleview::roots[static_cast<size_t> (std::clamp (s.root, 0, static_cast<int> (scaleview::roots.size()) - 1))];
    const auto& scale = scaleview::scales[static_cast<size_t> (std::clamp (s.scale, 0, static_cast<int> (scaleview::scales.size()) - 1))];
    return std::string (root.name) + " " + scale.name;
}

std::string noteName (const ScaleChoice& s, int pitch)
{
    const int octave = pitch / 12 - 1;
    return scaleview::chordNoteName (static_cast<int> (pcIndex (pitch)), keyOf (s)) + std::to_string (octave);
}

} // namespace kal::harmony
