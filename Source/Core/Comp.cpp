#include "Comp.h"

#include "Edit.h"

#include <algorithm>
#include <limits>

namespace kal::comp
{

namespace
{
TrackVersion* versionOf (Project& p, Id track)
{
    auto* t = p.track (track);
    return t && t->holdsClips() ? &t->version() : nullptr;
}

void tidy (std::vector<CompSegment>& segs)
{
    std::sort (segs.begin(), segs.end(), [] (const CompSegment& a, const CompSegment& b) { return a.start < b.start; });
    std::vector<CompSegment> out;
    for (auto& s : segs)
    {
        if (s.end <= s.start) continue;
        if (! out.empty() && out.back().take == s.take && out.back().end == s.start) out.back().end = s.end;
        else out.push_back (s);
    }
    segs = out;
}
} // namespace

std::pair<Tick, Tick> extent (const Take& t)
{
    Tick a = std::numeric_limits<Tick>::max(), b = 0;
    for (auto& c : t.clips) { a = std::min (a, c.start); b = std::max (b, c.end()); }
    if (t.clips.empty()) a = 0;
    return { a, b };
}

Id addTake (Project& p, Id track, std::vector<Clip> clips, const std::string& name)
{
    auto* v = versionOf (p, track);
    if (! v || clips.empty()) return 0;
    Take t;
    t.id = p.newId();
    t.name = name.empty() ? "Take " + std::to_string (v->takes.size() + 1) : name;
    for (auto& c : clips)
    {
        c.id = p.newId();
        c.fromTake = 0;
        for (auto& n : c.midi.notes) n.id = p.newId();
    }
    t.clips = std::move (clips);
    v->takes.push_back (t);
    const auto [a, b] = extent (v->takes.back());
    choose (p, track, t.id, a, b);
    return t.id;
}

void choose (Project& p, Id track, Id take, Tick from, Tick to)
{
    auto* v = versionOf (p, track);
    if (! v || to <= from) return;
    std::vector<CompSegment> segs;
    for (auto& s : v->comp)
    {
        if (s.end <= from || s.start >= to) { segs.push_back (s); continue; }
        if (s.start < from) segs.push_back ({ s.start, from, s.take });
        if (s.end > to) segs.push_back ({ to, s.end, s.take });
    }
    segs.push_back ({ from, to, take });
    tidy (segs);
    v->comp = segs;
    rebuild (p, *v);
}

void removeTake (Project& p, Id track, Id take)
{
    auto* v = versionOf (p, track);
    if (! v) return;
    v->takes.erase (std::remove_if (v->takes.begin(), v->takes.end(), [take] (const Take& t) { return t.id == take; }), v->takes.end());
    v->comp.erase (std::remove_if (v->comp.begin(), v->comp.end(), [take] (const CompSegment& s) { return s.take == take; }), v->comp.end());
    rebuild (p, *v);
}

void rebuild (Project& p, TrackVersion& v)
{
    v.clips.erase (std::remove_if (v.clips.begin(), v.clips.end(), [] (const Clip& c) { return c.fromTake != 0; }), v.clips.end());
    for (auto& seg : v.comp)
    {
        const auto take = std::find_if (v.takes.begin(), v.takes.end(), [&] (const Take& t) { return t.id == seg.take; });
        if (take == v.takes.end()) continue;
        for (auto& src : take->clips)
        {
            const Tick a = std::max (seg.start, src.start), b = std::min (seg.end, src.end());
            if (b <= a) continue;
            Clip c = src;
            c.id = p.newId();
            c.fromTake = take->id;
            c.offset += a - src.start;
            c.start = a;
            c.length = b - a;
            for (auto& n : c.midi.notes) n.id = p.newId();
            if (! c.isMidi())
            {
                // Joins between takes are faded so they don't click.
                if (a > src.start) c.audio.fadeIn = std::max (c.audio.fadeIn, joinFade);
                if (b < src.end()) c.audio.fadeOut = std::max (c.audio.fadeOut, joinFade);
            }
            v.clips.push_back (c);
        }
    }
    std::stable_sort (v.clips.begin(), v.clips.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
}

void flatten (Project& p, Id track)
{
    auto* v = versionOf (p, track);
    if (! v) return;
    for (auto& c : v->clips) c.fromTake = 0;
    v->takes.clear();
    v->comp.clear();
}

std::vector<Clip> audioPasses (const Clip& rec, double seconds, const TempoMap& tempo, Tick loopStart, Tick loopEnd)
{
    std::vector<Clip> out;
    const double recStart = tempo.secondsAt (rec.start);
    const double ls = tempo.secondsAt (loopStart), le = tempo.secondsAt (loopEnd);
    const double loopLength = le - ls;
    if (loopLength <= 0 || recStart >= le || recStart + seconds <= le)
    {
        Clip c = rec;
        c.length = std::max<Tick> (1, tempo.tickAt (recStart + seconds) - rec.start);
        out.push_back (c);
        return out;
    }
    // The first pass runs from where recording began to the loop's end.
    double fileAt = 0;
    double passStart = recStart;
    while (fileAt < seconds - 0.01)
    {
        const double passEnd = std::min (le, passStart + (seconds - fileAt));
        Clip c = rec;
        c.start = tempo.tickAt (passStart);
        c.length = std::max<Tick> (1, tempo.tickAt (passEnd) - c.start);
        c.audio.sourceStart = rec.audio.sourceStart + fileAt;
        c.audio.sourceLength = passEnd - passStart;
        out.push_back (c);
        fileAt += passEnd - passStart;
        passStart = ls;
    }
    return out;
}

std::vector<std::vector<capture::Event>> midiPasses (const std::vector<capture::Event>& events)
{
    std::vector<std::vector<capture::Event>> out (1);
    double last = -1;
    for (auto& e : events)
    {
        // The song going backwards is the loop going round.
        if (last >= 0 && e.songTicks + PPQ / 4 < last && ! out.back().empty()) out.emplace_back();
        out.back().push_back (e);
        last = e.songTicks;
    }
    if (out.back().empty()) out.pop_back();
    return out;
}

} // namespace kal::comp
