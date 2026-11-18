#include "StepInput.h"

#include <algorithm>

namespace kal
{

Tick StepInput::valueOf (int d)
{
    d = std::clamp (d, 1, 8);
    return (Tick) (PPQ * 4 * 128 / (1 << (8 - d))) / 128;   // 1 -> 1/128 ... 8 -> 1/1
}

void StepInput::keyDown (int pitch, int velocity)
{
    down[pitch] = velocity;
    chord[pitch] = velocity;
}

std::vector<Note> StepInput::keyUp (int pitch)
{
    down.erase (pitch);
    std::vector<Note> out;
    if (! down.empty() || chord.empty()) return out;
    for (auto& [p, v] : chord)
    {
        Note n;
        n.start = 0;
        n.length = length();
        n.pitch = p;
        n.velocity = std::clamp (v, 1, 127);
        out.push_back (n);
    }
    chord.clear();
    return out;
}

void StepInput::digit (int d)
{
    if (d == 9) { dottedNext = true; tripletNext = false; return; }
    if (d == 0) { tripletNext = true; dottedNext = false; return; }
    if (d < 1 || d > 8) return;
    value = d;
    dotted = dottedNext;
    triplet = tripletNext;
    dottedNext = tripletNext = false;
}

Tick StepInput::rest()
{
    Tick t = dottedNext || tripletNext ? valueOf (value) : length();
    if (dottedNext) t = t * 3 / 2;
    if (tripletNext) t = t * 2 / 3;
    dottedNext = tripletNext = false;
    return std::max<Tick> (1, t);
}

Tick StepInput::length() const
{
    Tick t = valueOf (value);
    if (dotted) t = t * 3 / 2;
    if (triplet) t = t * 2 / 3;
    return std::max<Tick> (1, t);
}

std::string StepInput::lengthName() const
{
    static const char* names[] { "1/128", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1" };
    std::string s = names[value - 1];
    if (dotted) s = "dotted " + s;
    if (triplet) s += " triplet";
    if (dottedNext) s += " (next: dotted)";
    if (tripletNext) s += " (next: triplet)";
    return s;
}

} // namespace kal
