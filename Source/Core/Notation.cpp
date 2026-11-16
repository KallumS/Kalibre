#include "Notation.h"

#include "ScaleModel.h"

#include <algorithm>
#include <map>

namespace kal::notation
{

namespace
{
constexpr int letterPc[7] { 0, 2, 4, 5, 7, 9, 11 };

int letterIndex (char c)
{
    switch (c)
    {
        case 'C': return 0; case 'D': return 1; case 'E': return 2; case 'F': return 3;
        case 'G': return 4; case 'A': return 5; case 'B': return 6; default: return 0;
    }
}

scaleview::Key keyOf (const ScaleChoice& s)
{
    return scaleview::buildKey (std::clamp (s.root, 0, static_cast<int> (scaleview::roots.size()) - 1),
                                std::clamp (s.scale, 0, static_cast<int> (scaleview::scales.size()) - 1));
}
} // namespace

Spelled spell (int pitch, const ScaleChoice& s)
{
    const auto key = keyOf (s);
    const int pc = ((pitch % 12) + 12) % 12;
    const auto& name = key.names[static_cast<size_t> (pc)];
    Spelled out;
    out.letter = letterIndex (name.empty() ? 'C' : name[0]);
    for (size_t i = 1; i < name.size(); ++i)
    {
        if (name[i] == '#') out.accidental += 1;
        else if (name[i] == 'x') out.accidental += 2;
        else if (name[i] == 'b') out.accidental -= 1;
    }
    // The octave belongs to the letter: B#3 sounds as C4, Cb4 as B3.
    out.octave = (pitch - letterPc[out.letter] - out.accidental) / 12 - 1;
    return out;
}

int staffStep (const Spelled& sp, Clef clef)
{
    const int diatonic = sp.octave * 7 + sp.letter;    // C4 = 28
    const int bottom = clef == Clef::Treble ? 4 * 7 + 2   // E4
                                            : 2 * 7 + 4;  // G2
    return diatonic - bottom;
}

int pitchAtStep (int step, Clef clef, const ScaleChoice& s, int nudge)
{
    const int bottom = clef == Clef::Treble ? 4 * 7 + 2 : 2 * 7 + 4;
    const int diatonic = bottom + step;
    const int octave = diatonic / 7 - (diatonic < 0 && diatonic % 7 != 0 ? 1 : 0);
    const int letter = ((diatonic % 7) + 7) % 7;
    int pitch = (octave + 1) * 12 + letterPc[letter];
    // The scale's own accidental on that letter, if it has one.
    const auto key = keyOf (s);
    for (int pc = 0; pc < 12; ++pc)
    {
        if (! key.lit[static_cast<size_t> (pc)]) continue;
        const auto& name = key.names[static_cast<size_t> (pc)];
        if (! name.empty() && letterIndex (name[0]) == letter && name[0] == "CDEFGAB"[letter])
        {
            int acc = 0;
            for (size_t i = 1; i < name.size(); ++i)
                acc += name[i] == '#' ? 1 : name[i] == 'x' ? 2 : name[i] == 'b' ? -1 : 0;
            pitch += acc;
            break;
        }
    }
    return std::clamp (pitch + nudge, 0, 127);
}

Clef clefFor (const std::vector<Note>& notes)
{
    if (notes.empty()) return Clef::Treble;
    double sum = 0;
    for (auto& n : notes) sum += n.pitch;
    return sum / static_cast<double> (notes.size()) < 57 ? Clef::Bass : Clef::Treble;
}

Tick ticksOf (Value v, bool dotted, bool triplet)
{
    Tick t = PPQ * 4;
    switch (v)
    {
        case Value::Whole:        t = PPQ * 4; break;
        case Value::Half:         t = PPQ * 2; break;
        case Value::Quarter:      t = PPQ; break;
        case Value::Eighth:       t = PPQ / 2; break;
        case Value::Sixteenth:    t = PPQ / 4; break;
        case Value::ThirtySecond: t = PPQ / 8; break;
    }
    if (dotted) t = t * 3 / 2;
    if (triplet) t = t * 2 / 3;
    return t;
}

std::vector<Duration> split (Tick length)
{
    std::vector<Duration> out;
    const Value values[] { Value::Whole, Value::Half, Value::Quarter, Value::Eighth, Value::Sixteenth, Value::ThirtySecond };
    Tick left = length;
    while (left >= ticksOf (Value::ThirtySecond))
    {
        bool placed = false;
        for (auto v : values)
        {
            if (ticksOf (v, true) <= left && v != Value::ThirtySecond)
            {
                out.push_back ({ v, true, ticksOf (v, true) });
                left -= ticksOf (v, true);
                placed = true;
                break;
            }
            if (ticksOf (v) <= left)
            {
                out.push_back ({ v, false, ticksOf (v) });
                left -= ticksOf (v);
                placed = true;
                break;
            }
        }
        if (! placed) break;
    }
    // Anything shorter than a 1/32 is shown as one.
    if (out.empty() && length > 0) out.push_back ({ Value::ThirtySecond, false, length });
    return out;
}

std::vector<Bar> layout (const Clip& clip, const TempoMap& tempo, const ScaleChoice& scale, Clef clef)
{
    std::vector<Bar> bars;
    if (! clip.isMidi()) return bars;

    // Notes as heard: in clip ticks from the clip's start.
    std::map<Tick, std::vector<const Note*>> starts;
    for (auto& n : clip.midi.notes)
    {
        const Tick at = n.start - clip.offset;
        if (at >= 0 && at < clip.length) starts[at].push_back (&n);
    }

    const int firstBar = tempo.barAt (clip.start);
    for (int b = firstBar; tempo.barStart (b) < clip.end(); ++b)
    {
        Bar bar;
        bar.index = b;
        const auto& m = tempo.meterAtBar (b);
        bar.numerator = m.numerator;
        bar.denominator = m.denominator;
        bar.start = std::max (tempo.barStart (b), clip.start) - clip.start;
        bar.length = std::min (tempo.barStart (b + 1), clip.end()) - clip.start - bar.start;
        bars.push_back (bar);
    }

    auto barOf = [&] (Tick t) -> Bar*
    {
        for (auto& bar : bars) if (t >= bar.start && t < bar.start + bar.length) return &bar;
        return nullptr;
    };

    // One voice: each chord lasts until the next start or its longest note's end.
    std::vector<std::pair<Tick, Tick>> sounding;   // (start, end) of each chord
    std::vector<std::vector<const Note*>> chords;
    for (auto it = starts.begin(); it != starts.end(); ++it)
    {
        Tick end = 0;
        for (auto* n : it->second) end = std::max (end, n->start - clip.offset + n->length);
        auto next = std::next (it);
        if (next != starts.end()) end = std::min (end, next->first);
        end = std::min (end, clip.length);
        sounding.push_back ({ it->first, end });
        chords.push_back (it->second);
    }

    auto place = [&] (Tick from, Tick to, const std::vector<const Note*>* chord)
    {
        // Broken at bar lines, then into note values, tied when it is a note.
        Tick t = from;
        while (t < to)
        {
            Bar* bar = barOf (t);
            if (! bar) break;
            const Tick segEnd = std::min (to, bar->start + bar->length);
            const auto parts = split (segEnd - t);
            for (size_t i = 0; i < parts.size(); ++i)
            {
                Glyph g;
                g.start = t;
                g.duration = parts[i];
                g.rest = chord == nullptr;
                if (chord)
                {
                    for (auto* n : *chord) g.pitches.push_back (n->pitch);
                    std::sort (g.pitches.begin(), g.pitches.end());
                    g.pitches.erase (std::unique (g.pitches.begin(), g.pitches.end()), g.pitches.end());
                    for (int p : g.pitches)
                    {
                        g.spelled.push_back (spell (p, scale));
                        g.steps.push_back (staffStep (g.spelled.back(), clef));
                    }
                    g.tieToNext = (i + 1 < parts.size()) || segEnd < to;
                }
                bar->glyphs.push_back (g);
                t += parts[i].ticks;
            }
            t = segEnd;
        }
    };

    Tick cursor = 0;
    for (size_t i = 0; i < chords.size(); ++i)
    {
        if (sounding[i].first > cursor) place (cursor, sounding[i].first, nullptr);
        place (sounding[i].first, sounding[i].second, &chords[i]);
        cursor = sounding[i].second;
    }
    if (cursor < clip.length) place (cursor, clip.length, nullptr);
    return bars;
}

} // namespace kal::notation
