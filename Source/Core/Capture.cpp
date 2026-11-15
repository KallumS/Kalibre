#include "Capture.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace kal::capture
{

Result lastPerformance (const std::vector<Event>& all, const TempoMap& tempo, Tick cursor, double gapSeconds)
{
    Result r;
    if (all.empty()) return r;

    // Walk back to a gap with nothing held.
    size_t first = all.size() - 1;
    {
        int held = 0;
        for (size_t i = all.size(); i-- > 0;)
        {
            const auto& e = all[i];
            const int type = e.bytes[0] & 0xF0;
            if (type == 0x80 || (type == 0x90 && e.bytes[2] == 0)) ++held;
            else if (type == 0x90) held = std::max (0, held - 1);
            first = i;
            if (i > 0 && held == 0 && e.wallSeconds - all[i - 1].wallSeconds >= gapSeconds) break;
        }
    }
    std::vector<Event> take (all.begin() + static_cast<std::ptrdiff_t> (first), all.end());
    // Leading note-offs belong to notes from before the gap.
    while (! take.empty() && ((take.front().bytes[0] & 0xF0) == 0x80
                              || ((take.front().bytes[0] & 0xF0) == 0x90 && take.front().bytes[2] == 0)))
        take.erase (take.begin());
    if (take.empty()) return r;

    // Played with the song running: every event has its own place in the song.
    r.wasPlaying = take.front().songTicks >= 0;
    const double startSeconds = take.front().wallSeconds;
    const double cursorSeconds = tempo.secondsAt (cursor);
    auto tickOf = [&] (const Event& e) -> double
    {
        if (r.wasPlaying && e.songTicks >= 0) return e.songTicks;
        return tempo.ticksAt (cursorSeconds + (e.wallSeconds - startSeconds));
    };

    const Tick clipStart = r.wasPlaying ? tempo.barStart (tempo.barAt (static_cast<Tick> (tickOf (take.front()))))
                                        : cursor;
    r.start = clipStart;

    std::map<std::pair<int, int>, std::pair<Tick, int>> open;   // (channel, pitch) -> (start, velocity)
    Tick last = clipStart;
    for (const auto& e : take)
    {
        const Tick t = static_cast<Tick> (std::llround (tickOf (e)));
        last = std::max (last, t);
        const int type = e.bytes[0] & 0xF0, ch = e.bytes[0] & 0x0F;
        if (type == 0x90 && e.bytes[2] > 0)
        {
            open[{ ch, e.bytes[1] }] = { t, e.bytes[2] };
        }
        else if (type == 0x80 || type == 0x90)
        {
            auto it = open.find ({ ch, e.bytes[1] });
            if (it == open.end()) continue;
            Note n;
            n.start = it->second.first - clipStart;
            n.length = std::max<Tick> (1, t - it->second.first);
            n.pitch = e.bytes[1];
            n.velocity = it->second.second;
            n.channel = ch;
            r.notes.push_back (n);
            open.erase (it);
        }
        else if (type == 0xB0 || type == 0xE0 || type == 0xD0 || type == 0xC0 || type == 0xA0)
        {
            MidiEvent m;
            m.tick = t - clipStart;
            m.channel = ch;
            switch (type)
            {
                case 0xB0: m.type = EventType::Controller; m.number = e.bytes[1]; m.value = e.bytes[2]; break;
                case 0xE0: m.type = EventType::PitchBend; m.value = e.bytes[1] | (e.bytes[2] << 7); break;
                case 0xD0: m.type = EventType::ChannelPressure; m.value = e.bytes[1]; break;
                case 0xA0: m.type = EventType::PolyPressure; m.number = e.bytes[1]; m.value = e.bytes[2]; break;
                default:   m.type = EventType::ProgramChange; m.value = e.bytes[1]; break;
            }
            r.events.push_back (m);
        }
    }
    for (auto& [key, v] : open)
    {
        Note n;
        n.start = v.first - clipStart;
        n.length = std::max<Tick> (PPQ / 8, last - v.first);
        n.pitch = key.second;
        n.velocity = v.second;
        n.channel = key.first;
        r.notes.push_back (n);
    }
    std::sort (r.notes.begin(), r.notes.end(), [] (const Note& a, const Note& b)
    {
        return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
    });
    Tick end = 0;
    for (auto& n : r.notes) end = std::max (end, n.end());
    // The clip runs to the end of the bar the performance ends in.
    const int endBar = tempo.barAt (clipStart + std::max<Tick> (1, end) - 1) + 1;
    r.length = std::max<Tick> (tempo.barStart (endBar) - clipStart, PPQ);
    return r;
}

} // namespace kal::capture
