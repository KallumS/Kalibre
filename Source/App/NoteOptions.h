/*
    NoteOptions - what the piano roll's velocity lane, the step sequencer and
    the drum grid can set for each note (decision 0061): its velocity, the
    chance it plays, how far its velocity may wander, how far its timing may.
    One menu, the same in all three.
*/

#pragma once

#include "Model.h"
#include "Theme.h"

namespace kal::noteopt
{

enum class Kind { Velocity, Chance, VelocityRange, Timing };

inline juce::String name (Kind k)
{
    switch (k)
    {
        case Kind::Chance:        return "Note Chance";
        case Kind::VelocityRange: return "Random Velocity Range";
        case Kind::Timing:        return "Random Timing";
        default:                  return "Velocity";
    }
}

inline juce::String tip (Kind k)
{
    switch (k)
    {
        case Kind::Chance:        return "How often the note plays, out of 100: at 50% it plays about every other time.";
        case Kind::VelocityRange: return "How much softer or harder the note may play, chosen afresh each time: 10 is up to 10 either way.";
        case Kind::Timing:        return "How many milliseconds early or late the note may play, chosen afresh each time.";
        default:                  return "How hard the note is played.";
    }
}

inline int lowest (Kind k) { return k == Kind::Velocity ? 1 : 0; }
inline int highest (Kind k) { return k == Kind::Chance ? 100 : k == Kind::VelocityRange ? 64 : k == Kind::Timing ? 50 : 127; }

// The value as read: "100", "75%", "±10", "±8 ms".
inline juce::String format (Kind k, double v)
{
    const int i = juce::roundToInt (v);
    switch (k)
    {
        case Kind::Chance:        return juce::String (i) + "%";
        case Kind::VelocityRange: return juce::String::fromUTF8 ("\xc2\xb1") + juce::String (i);
        case Kind::Timing:        return juce::String::fromUTF8 ("\xc2\xb1") + juce::String (i) + " ms";
        default:                  return juce::String (i);
    }
}

// Whether a value is other than a plain note's: worth showing on it.
inline bool notPlain (Kind k, double v)
{
    return k == Kind::Chance ? v < 100 : k == Kind::Velocity ? false : v > 0;
}

inline double get (const Note& n, Kind k)
{
    switch (k)
    {
        case Kind::Chance:        return n.chance;
        case Kind::VelocityRange: return n.velocityRange;
        case Kind::Timing:        return n.timingMs;
        default:                  return n.velocity;
    }
}

inline void set (Note& n, Kind k, double v)
{
    const int i = juce::jlimit (lowest (k), highest (k), juce::roundToInt (v));
    switch (k)
    {
        case Kind::Chance:        n.chance = i; break;
        case Kind::VelocityRange: n.velocityRange = i; break;
        case Kind::Timing:        n.timingMs = i; break;
        default:                  n.velocity = i; break;
    }
}

// The menu: the four, the chosen one ticked.
inline void addTo (juce::PopupMenu& m, Kind chosen, std::function<void (Kind)> pick)
{
    for (auto k : { Kind::Velocity, Kind::Chance, Kind::VelocityRange, Kind::Timing })
        m.addItem (name (k), true, chosen == k, [pick, k] { pick (k); });
}

} // namespace kal::noteopt
