/*
    NoteOptions - what the piano roll's velocity lane, the step sequencer and
    the drum grid can set for each note (decision 0061): its velocity, the
    chance it plays, how far its velocity may wander, how far its timing may.
    One menu, the same in all three; the step sequencer and drum grid add
    Repeat, a step played several times in its length (decision 0063).
*/

#pragma once

#include "Model.h"
#include "Theme.h"

namespace kal::noteopt
{

enum class Kind { Velocity, Chance, VelocityRange, Timing, Repeat, Delay };

inline juce::String name (Kind k)
{
    switch (k)
    {
        case Kind::Chance:        return "Note Chance";
        case Kind::VelocityRange: return "Random Velocity Range";
        case Kind::Timing:        return "Random Timing";
        case Kind::Repeat:        return "Repeat";
        case Kind::Delay:         return "Note Delay";
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
        case Kind::Repeat:        return "How many times the step plays in its length: 2 is two quick notes, 4 a roll.";
        case Kind::Delay:         return "How many milliseconds later (or, below the middle, earlier) the note plays, on top of its track's and articulation's delays.";
        default:                  return "How hard the note is played.";
    }
}

inline int lowest (Kind k) { return k == Kind::Velocity || k == Kind::Repeat ? 1 : k == Kind::Delay ? -250 : 0; }
inline int highest (Kind k) { return k == Kind::Chance ? 100 : k == Kind::VelocityRange ? 64 : k == Kind::Timing ? 50 : k == Kind::Repeat ? 8 : k == Kind::Delay ? 250 : 127; }
inline bool signedValue (Kind k) { return k == Kind::Delay; }   // drawn from the middle

// The value as read: "100", "75%", "±10", "±8 ms".
inline juce::String format (Kind k, double v)
{
    const int i = juce::roundToInt (v);
    switch (k)
    {
        case Kind::Chance:        return juce::String (i) + "%";
        case Kind::VelocityRange: return juce::String::fromUTF8 ("\xc2\xb1") + juce::String (i);
        case Kind::Timing:        return juce::String::fromUTF8 ("\xc2\xb1") + juce::String (i) + " ms";
        case Kind::Repeat:        return juce::String::fromUTF8 ("\xc3\x97") + juce::String (i);
        case Kind::Delay:         return (i > 0 ? "+" : i < 0 ? juce::String::fromUTF8 ("\xe2\x88\x92") : juce::String()) + juce::String (std::abs (i)) + " ms";
        default:                  return juce::String (i);
    }
}

// Whether a value is other than a plain note's: worth showing on it.
inline bool notPlain (Kind k, double v)
{
    return k == Kind::Chance ? v < 100 : k == Kind::Velocity ? false : k == Kind::Repeat ? v > 1 : k == Kind::Delay ? v != 0 : v > 0;
}

inline double get (const Note& n, Kind k)
{
    switch (k)
    {
        case Kind::Chance:        return n.chance;
        case Kind::VelocityRange: return n.velocityRange;
        case Kind::Timing:        return n.timingMs;
        case Kind::Repeat:        return std::max (1, n.repeat);
        case Kind::Delay:         return n.delayMs;
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
        case Kind::Repeat:        break;   // the pattern editors' own: a note's repeats are notes
        case Kind::Delay:         n.delayMs = i; break;
        default:                  n.velocity = i; break;
    }
}

// The menu: the four (and Repeat, in the pattern editors; Note Delay in the
// piano roll, decision 0082), the chosen one ticked.
inline void addTo (juce::PopupMenu& m, Kind chosen, std::function<void (Kind)> pick, bool withRepeat = false, bool withDelay = false)
{
    for (auto k : { Kind::Velocity, Kind::Chance, Kind::VelocityRange, Kind::Timing, Kind::Repeat, Kind::Delay })
        if ((k != Kind::Repeat || withRepeat) && (k != Kind::Delay || withDelay))
            m.addItem (name (k), true, chosen == k, [pick, k] { pick (k); });
}

} // namespace kal::noteopt
