#include "Mpe.h"

#include <algorithm>
#include <cmath>

namespace kal::mpe
{

int bendValue (double semitones)
{
    return std::clamp ((int) std::lround (8192.0 + semitones / bendRange * 8192.0), 0, 16383);
}

NoteExpression expressionAt (const Note& n, Tick t)
{
    NoteExpression out;
    out.tick = t;
    for (auto& e : n.expression)
    {
        if (e.tick > t) break;
        out.bend = e.bend;
        out.pressure = e.pressure;
        out.timbre = e.timbre;
    }
    return out;
}

void gatherExpression (MidiData& m)
{
    std::vector<bool> used (m.events.size(), false);
    for (auto& n : m.notes)
    {
        if (n.channel == 0) continue;   // channel 1 is the master's: it moves every note
        NoteExpression now;
        now.tick = 0;
        for (size_t i = 0; i < m.events.size(); ++i)
        {
            const auto& e = m.events[i];
            if (e.channel != n.channel || e.tick < n.start || e.tick > n.end()) continue;
            if (e.type == EventType::PitchBend) now.bend = (e.value - 8192) / 8192.0 * bendRange;
            else if (e.type == EventType::ChannelPressure) now.pressure = e.value / 127.0;
            else if (e.type == EventType::Controller && e.number == 74) now.timbre = e.value / 127.0;
            else continue;
            now.tick = e.tick - n.start;
            if (! n.expression.empty() && n.expression.back().tick == now.tick) n.expression.back() = now;
            else n.expression.push_back (now);
            used[i] = true;
        }
    }
    std::vector<MidiEvent> rest;
    for (size_t i = 0; i < m.events.size(); ++i) if (! used[i]) rest.push_back (m.events[i]);
    m.events = std::move (rest);
}

} // namespace kal::mpe
