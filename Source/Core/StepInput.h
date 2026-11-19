/*
    StepInput - writing notes from a MIDI keyboard one step at a time
    (decision 0076), in the piano roll, notation and drum grid.

    Hold keys on the keyboard; press a number on the computer's keyboard for
    how long they are: 1 = 1/128, 2 = 1/64, 3 = 1/32, 4 = 1/16, 5 = 1/8,
    6 = 1/4, 7 = 1/2, 8 = 1/1. 9 first makes the next one dotted (9 then 3
    is a dotted 1/32), 0 first makes it a triplet. Let go of the keys and
    they're written at that length where the cursor is, which moves on by it.
    The length stays chosen for the next chord until another number is pressed.
    The - key is a rest that long: the cursor moves on, nothing written (9 or
    0 before it: a dotted or triplet rest, the notes' length left as it was).

    No JUCE in here.
*/

#pragma once

#include "Model.h"

#include <map>
#include <string>
#include <vector>

namespace kal
{

class StepInput
{
public:
    void keyDown (int pitch, int velocity);
    // When the last key held lets go: every key held since the last chord, as
    // notes from 0, length() long. Empty while keys are still down.
    std::vector<Note> keyUp (int pitch);
    // A number pressed: 1-8 a length, 9 dotted next, 0 triplet next.
    void digit (int d);
    // The - key: how long a rest to leave (the length chosen, dotted or a
    // triplet if 9 or 0 came just before).
    Tick rest();

    Tick length() const;
    std::string lengthName() const;           // "1/16", "dotted 1/32", "1/8 triplet"
    bool holding() const { return ! down.empty(); }
    void reset() { down.clear(); chord.clear(); }

    // 1/128 to 1/1, by the number pressed (1-8).
    static Tick valueOf (int d);

private:
    int value = 4;                 // 1/16 to begin with
    bool dotted = false, triplet = false;
    bool dottedNext = false, tripletNext = false;
    std::map<int, int> down;       // pitch -> velocity, held now
    std::map<int, int> chord;      // every key held since the last chord was written
};

} // namespace kal
