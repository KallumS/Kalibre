#include "Patterns.h"

#include "Edit.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace kal::patterns
{

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

std::vector<Lane> standardKit (int steps)
{
    const std::vector<std::pair<const char*, int>> kit {
        { "Crash", 49 }, { "Ride", 51 }, { "Open Hat", 46 }, { "Closed Hat", 42 },
        { "High Tom", 50 }, { "Mid Tom", 47 }, { "Low Tom", 45 },
        { "Clap", 39 }, { "Snare", 38 }, { "Rim", 37 }, { "Kick", 36 },
    };
    std::vector<Lane> lanes;
    for (auto& [name, pitch] : kit)
        lanes.push_back ({ name, pitch, std::vector<int> (static_cast<size_t> (steps), 0) });
    return lanes;
}

static Tick stepStart (const Grid& g, int step)
{
    Tick t = static_cast<Tick> (step) * g.stepTicks;
    if (step % 2 == 1) t += static_cast<Tick> (std::lround (g.swing * static_cast<double> (g.stepTicks)));
    return t;
}

Grid readGrid (const MidiData& m, Grid g, Tick from)
{
    for (auto& l : g.lanes) l.velocity.assign (static_cast<size_t> (g.steps), 0);
    const Tick span = g.stepTicks * g.steps;
    for (auto& n : m.notes)
    {
        const Tick rel = n.start - from;
        if (rel < -g.stepTicks / 2 || rel >= span - g.stepTicks / 2) continue;
        const int step = std::clamp (static_cast<int> (std::lround (static_cast<double> (rel) / g.stepTicks)), 0, g.steps - 1);
        auto it = std::find_if (g.lanes.begin(), g.lanes.end(), [&] (const Lane& l) { return l.pitch == n.pitch; });
        if (it == g.lanes.end())
        {
            g.lanes.push_back ({ "Note " + std::to_string (n.pitch), n.pitch, std::vector<int> (static_cast<size_t> (g.steps), 0) });
            it = g.lanes.end() - 1;
        }
        it->velocity[static_cast<size_t> (step)] = std::max (it->velocity[static_cast<size_t> (step)], n.velocity);
    }
    return g;
}

void writeGrid (MidiData& m, const Grid& g, Tick from, Tick length, unsigned& nextId)
{
    std::set<int> pitches;
    for (auto& l : g.lanes) pitches.insert (l.pitch);
    m.notes.erase (std::remove_if (m.notes.begin(), m.notes.end(), [&] (const Note& n)
    {
        return pitches.count (n.pitch) && n.start >= from && n.start < from + length;
    }), m.notes.end());

    const Tick span = g.stepTicks * g.steps;
    const Tick noteLen = std::max<Tick> (1, static_cast<Tick> (std::lround (g.gate * static_cast<double> (g.stepTicks))));
    for (Tick base = from; base < from + length; base += span)
        for (auto& l : g.lanes)
            for (int s = 0; s < g.steps && s < static_cast<int> (l.velocity.size()); ++s)
            {
                const int v = l.velocity[static_cast<size_t> (s)];
                if (v <= 0) continue;
                const Tick at = base + stepStart (g, s);
                if (at >= from + length) continue;
                Note n;
                n.id = nextId++;
                n.start = at;
                n.length = noteLen;
                n.pitch = l.pitch;
                n.velocity = std::clamp (v, 1, 127);
                m.notes.push_back (n);
            }
    edit::sortNotes (m);
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
StepSequence readSteps (const MidiData& m, Tick stepTicks, int count, Tick from)
{
    StepSequence s;
    s.stepTicks = stepTicks;
    s.steps.assign (static_cast<size_t> (count), Step {});
    for (auto& n : m.notes)
    {
        const Tick rel = n.start - from;
        if (rel < 0 || rel >= stepTicks * count) continue;
        const int i = static_cast<int> (rel / stepTicks);
        auto& st = s.steps[static_cast<size_t> (i)];
        // One note a step: the highest wins.
        if (st.on && st.pitch >= n.pitch) continue;
        st.on = true;
        st.pitch = n.pitch;
        st.velocity = n.velocity;
        const double share = static_cast<double> (n.length) / static_cast<double> (stepTicks);
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

void writeSteps (MidiData& m, const StepSequence& s, Tick from, Tick length, unsigned& nextId)
{
    m.notes.erase (std::remove_if (m.notes.begin(), m.notes.end(), [&] (const Note& n)
    {
        return n.start >= from && n.start < from + length;
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
            const Tick len = static_cast<Tick> (last - i) * s.stepTicks
                           + std::max<Tick> (1, static_cast<Tick> (std::lround (lastGate * static_cast<double> (s.stepTicks))));
            Note n;
            n.id = nextId++;
            n.start = at;
            n.length = len;
            n.pitch = std::clamp (st.pitch, 0, 127);
            n.velocity = std::clamp (st.velocity, 1, 127);
            m.notes.push_back (n);
        }
    }
    edit::sortNotes (m);
}

} // namespace kal::patterns
