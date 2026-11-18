#include "Patterns.h"

#include "Edit.h"

#include <algorithm>
#include <limits>
#include <random>
#include <cmath>
#include <map>
#include <set>

namespace kal::patterns
{

Window windowOf (const Clip& c)
{
    if (c.loopLength > 0) return { 0, c.loopLength };
    return { c.offset, c.length };
}


std::vector<bool> euclid (int steps, int pulses, int rotation)
{
    steps = std::clamp (steps, 1, 128);
    pulses = std::clamp (pulses, 0, steps);
    std::vector<bool> out (static_cast<size_t> (steps), false);
    if (pulses == 0) return out;
    // Bjorklund's algorithm: pair the pulses with the rests, then the
    // leftovers with the pairs, until one group or none is left over.
    std::vector<std::vector<bool>> a (static_cast<size_t> (pulses), std::vector<bool> { true });
    std::vector<std::vector<bool>> b (static_cast<size_t> (steps - pulses), std::vector<bool> { false });
    while (b.size() > 1)
    {
        const size_t n = std::min (a.size(), b.size());
        std::vector<std::vector<bool>> joined;
        for (size_t i = 0; i < n; ++i)
        {
            auto x = a[i];
            x.insert (x.end(), b[i].begin(), b[i].end());
            joined.push_back (x);
        }
        std::vector<std::vector<bool>> rest;
        if (a.size() > n) rest.assign (a.begin() + static_cast<std::ptrdiff_t> (n), a.end());
        else rest.assign (b.begin() + static_cast<std::ptrdiff_t> (n), b.end());
        a = joined;
        b = rest;
    }
    size_t k = 0;
    for (auto* group : { &a, &b })
        for (auto& g : *group)
            for (bool x : g) out[k++] = x;
    rotation = ((rotation % steps) + steps) % steps;
    std::rotate (out.begin(), out.begin() + (steps - rotation) % steps, out.end());
    return out;
}

std::string drumName (int pitch)
{
    static const char* names[] {
        "Acoustic Bass Drum", "Kick", "Rim", "Snare", "Clap", "Electric Snare", "Low Floor Tom", "Closed Hat", "High Floor Tom",
        "Pedal Hi-Hat", "Low Tom", "Open Hat", "Low-Mid Tom", "Hi-Mid Tom", "Crash", "High Tom", "Ride", "Chinese Cymbal",
        "Ride Bell", "Tambourine", "Splash", "Cowbell", "Crash 2", "Vibraslap", "Ride 2", "High Bongo", "Low Bongo",
        "Mute High Conga", "Open High Conga", "Low Conga", "High Timbale", "Low Timbale", "High Agogo", "Low Agogo", "Cabasa",
        "Maracas", "Short Whistle", "Long Whistle", "Short Guiro", "Long Guiro", "Claves", "High Wood Block", "Low Wood Block",
        "Mute Cuica", "Open Cuica", "Mute Triangle", "Open Triangle" };
    if (pitch >= 35 && pitch <= 81) return names[pitch - 35];
    return "Note " + std::to_string (pitch);
}

std::vector<Lane> standardKit (int steps)
{
    const std::vector<std::pair<const char*, int>> kit {
        { "Crash", 49 }, { "Ride", 51 }, { "Open Hat", 46 }, { "Closed Hat", 42 },
        { "High Tom", 50 }, { "Mid Tom", 47 }, { "Low Tom", 45 },
        { "Clap", 39 }, { "Snare", 38 }, { "Rim", 37 }, { "Kick", 36 },
    };
    std::vector<Lane> lanes;
    for (auto& [name, pitch] : kit)
        lanes.push_back ({ name, pitch, std::vector<int> (static_cast<size_t> (steps), 0), {}, {}, {}, {} });
    return lanes;
}

static Tick stepStart (const Grid& g, int step)
{
    Tick t = static_cast<Tick> (step) * g.stepTicks;
    if (step % 2 == 1) t += static_cast<Tick> (std::lround (g.swing * static_cast<double> (g.stepTicks)));
    return t;
}

Grid readGrid (const MidiData& m, Grid g, Tick from, NoteFrom who)
{
    const size_t steps = static_cast<size_t> (g.steps);
    auto blank = [steps] (Lane& l)
    {
        l.velocity.assign (steps, 0);
        l.chance.assign (steps, 100);
        l.velocityRange.assign (steps, 0);
        l.timingMs.assign (steps, 0.0);
        l.repeat.assign (steps, 1);
    };
    for (auto& l : g.lanes) blank (l);
    const Tick span = g.stepTicks * g.steps;
    for (auto& n : m.notes)
    {
        if (n.from != who || n.repeat == 0) continue;   // a repeat belongs to the hit before it (decision 0063)
        const Tick rel = n.start - from;
        if (rel < -g.stepTicks / 2) continue;
        // The step whose (swung) start is nearest; nearer the next pass's first, it's that pass's.
        int step = -1;
        Tick best = std::numeric_limits<Tick>::max();
        const int around = static_cast<int> (rel / std::max<Tick> (1, g.stepTicks));
        for (int s = std::max (0, around - 1); s <= std::min (g.steps, around + 1); ++s)
        {
            const Tick at = s == g.steps ? span : stepStart (g, s);
            if (std::abs (rel - at) < best) { best = std::abs (rel - at); step = s; }
        }
        if (step < 0 || step >= g.steps) continue;
        auto it = std::find_if (g.lanes.begin(), g.lanes.end(), [&] (const Lane& l) { return l.pitch == n.pitch; });
        if (it == g.lanes.end())
        {
            g.lanes.push_back ({ drumName (n.pitch), n.pitch, {}, {}, {}, {}, {} });
            blank (g.lanes.back());
            it = g.lanes.end() - 1;
        }
        const auto s = static_cast<size_t> (step);
        if (n.velocity >= it->velocity[s])
        {
            it->velocity[s] = n.velocity;
            it->chance[s] = n.chance;
            it->velocityRange[s] = n.velocityRange;
            it->timingMs[s] = n.timingMs;
            it->repeat[s] = std::clamp (n.repeat, 1, 8);
        }
    }
    return g;
}

void writeGrid (MidiData& m, const Grid& g, Tick from, Tick length, unsigned& nextId, NoteFrom who)
{
    std::set<int> pitches;
    for (auto& l : g.lanes) pitches.insert (l.pitch);
    m.notes.erase (std::remove_if (m.notes.begin(), m.notes.end(), [&] (const Note& n)
    {
        return n.from == who && pitches.count (n.pitch) && n.start >= from && n.start < from + length;
    }), m.notes.end());

    const Tick span = g.stepTicks * g.steps;
    for (Tick base = from; base < from + length; base += span)
        for (auto& l : g.lanes)
            for (int s = 0; s < g.steps && s < static_cast<int> (l.velocity.size()); ++s)
            {
                const int v = l.velocity[static_cast<size_t> (s)];
                if (v <= 0) continue;
                const auto i = static_cast<size_t> (s);
                // Repeated, the step is shared out among its repeats, each gated (decision 0063).
                const int times = i < l.repeat.size() ? std::clamp (l.repeat[i], 1, 8) : 1;
                const Tick each = std::max<Tick> (1, g.stepTicks / times);
                const Tick noteLen = std::max<Tick> (1, static_cast<Tick> (std::lround (g.gate * static_cast<double> (each))));
                for (int k = 0; k < times; ++k)
                {
                    const Tick at = base + stepStart (g, s) + each * k;
                    if (at >= from + length) continue;
                    Note n;
                    n.id = nextId++;
                    n.start = at;
                    n.length = noteLen;
                    n.pitch = l.pitch;
                    n.velocity = std::clamp (v, 1, 127);
                    n.from = who;
                    n.repeat = k == 0 ? times : 0;
                    if (i < l.chance.size()) n.chance = l.chance[i];
                    if (i < l.velocityRange.size()) n.velocityRange = l.velocityRange[i];
                    if (i < l.timingMs.size()) n.timingMs = l.timingMs[i];
                    m.notes.push_back (n);
                }
            }
    edit::sortNotes (m);
}

EuclidSettings randomEuclid (int pitch, unsigned seed)
{
    std::mt19937 dice (seed * 2654435761u + (unsigned) pitch);
    auto pick = [&] (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (dice); };
    EuclidSettings e;
    const int lengths[] { 16, 16, 16, 8, 12, 32 };
    e.steps = lengths[pick (0, 5)];
    const int per8 = e.steps / 8;   // hits are counted per eight steps
    switch (pitch)
    {
        case 35: case 36:           e.pulses = std::max (1, pick (2, 3) * per8 + pick (0, 1)); e.rotation = pick (0, 1) ? 0 : pick (0, e.steps - 1); break;
        case 38: case 39: case 40:  e.pulses = std::max (1, pick (1, 2) * per8); e.rotation = pick (0, 2) ? e.steps / (2 * e.pulses) : pick (0, e.steps - 1); break;
        case 42: case 44: case 46:  e.pulses = std::max (1, pick (3, 7) * per8); e.rotation = pick (0, e.steps - 1); break;
        default:                    e.pulses = std::max (1, pick (1, 5) * per8); e.rotation = pick (0, e.steps - 1); break;
    }
    e.pulses = std::clamp (e.pulses, 1, e.steps - 1);
    e.rotation = ((e.rotation % e.steps) + e.steps) % e.steps;
    return e;
}

void euclidLane (Lane& lane, int steps, int pulses, int rotation, int velocity, int accentEvery)
{
    const auto hits = euclid (steps, pulses, rotation);
    lane.velocity.assign (static_cast<size_t> (steps), 0);
    int count = 0;
    for (size_t i = 0; i < hits.size(); ++i)
        if (hits[i])
        {
            const bool accent = accentEvery > 0 && count % accentEvery == 0;
            lane.velocity[i] = std::clamp (accent ? velocity + 20 : velocity, 1, 127);
            ++count;
        }
}

//==============================================================================
StepSequence readSteps (const MidiData& m, Tick stepTicks, int count, Tick from, NoteFrom who)
{
    StepSequence s;
    s.stepTicks = stepTicks;
    s.steps.assign (static_cast<size_t> (count), Step {});
    for (auto& n : m.notes)
    {
        if (n.from != who || n.repeat == 0) continue;   // a repeat belongs to the note before it (decision 0063)
        const Tick rel = n.start - from;
        if (rel < 0 || rel >= stepTicks * count) continue;
        const int i = static_cast<int> (rel / stepTicks);
        auto& st = s.steps[static_cast<size_t> (i)];
        // One note a step: the highest wins.
        if (st.on && st.pitch >= n.pitch) continue;
        st.on = true;
        st.pitch = n.pitch;
        st.velocity = n.velocity;
        st.chance = n.chance;
        st.velocityRange = n.velocityRange;
        st.timingMs = n.timingMs;
        st.repeat = std::clamp (n.repeat, 1, 8);
        // A repeated note's gate is of its share of the step.
        const double share = static_cast<double> (n.length) * st.repeat / static_cast<double> (stepTicks);
        st.gate = std::clamp (share, 0.05, 1.0);
        // A note longer than its step ties into the steps it covers.
        const int covered = static_cast<int> ((n.length - 1) / stepTicks);
        for (int k = 0; k < covered && i + k + 1 < count; ++k)
        {
            s.steps[static_cast<size_t> (i + k)].tie = true;
            auto& next = s.steps[static_cast<size_t> (i + k + 1)];
            next.on = true;
            next.pitch = n.pitch;
            next.velocity = n.velocity;
            next.gate = std::clamp (share - (k + 1), 0.05, 1.0);
        }
    }
    return s;
}

void writeSteps (MidiData& m, const StepSequence& s, Tick from, Tick length, unsigned& nextId, NoteFrom who)
{
    m.notes.erase (std::remove_if (m.notes.begin(), m.notes.end(), [&] (const Note& n)
    {
        return n.from == who && n.start >= from && n.start < from + length;
    }), m.notes.end());
    const int count = static_cast<int> (s.steps.size());
    const Tick span = s.stepTicks * count;
    for (Tick base = from; base < from + length; base += span)
    {
        for (int i = 0; i < count; ++i)
        {
            const auto& st = s.steps[static_cast<size_t> (i)];
            if (! st.on) continue;
            // A tied step continues the note before it.
            if (i > 0 && s.steps[static_cast<size_t> (i - 1)].on && s.steps[static_cast<size_t> (i - 1)].tie
                && s.steps[static_cast<size_t> (i - 1)].pitch == st.pitch)
                continue;
            int last = i;
            while (s.steps[static_cast<size_t> (last)].tie && last + 1 < count
                   && s.steps[static_cast<size_t> (last + 1)].on && s.steps[static_cast<size_t> (last + 1)].pitch == st.pitch)
                ++last;
            Tick at = base + static_cast<Tick> (i) * s.stepTicks;
            if (i % 2 == 1) at += static_cast<Tick> (std::lround (s.swing * static_cast<double> (s.stepTicks)));
            if (at >= from + length) continue;
            const double lastGate = s.steps[static_cast<size_t> (last)].gate;
            // A step on its own may repeat in its length, each repeat gated (decision 0063);
            // a tied run is one long note.
            const int times = last == i ? std::clamp (st.repeat, 1, 8) : 1;
            const Tick each = last == i ? std::max<Tick> (1, s.stepTicks / times) : s.stepTicks;
            const Tick len = static_cast<Tick> (last - i) * s.stepTicks
                           + std::max<Tick> (1, static_cast<Tick> (std::lround (lastGate * static_cast<double> (each))));
            for (int k = 0; k < times; ++k)
            {
                if (at + each * k >= from + length) continue;
                Note n;
                n.id = nextId++;
                n.start = at + each * k;
                n.length = len;
                n.pitch = std::clamp (st.pitch, 0, 127);
                n.velocity = std::clamp (st.velocity, 1, 127);
                n.from = who;
                n.chance = st.chance;
                n.velocityRange = st.velocityRange;
                n.timingMs = st.timingMs;
                n.repeat = k == 0 ? times : 0;
                m.notes.push_back (n);
            }
        }
    }
    edit::sortNotes (m);
}

} // namespace kal::patterns
