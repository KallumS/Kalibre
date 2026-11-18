// Step input (decision 0076): keys held, a number for the length, written on letting go.
#include "Check.h"
#include "StepInput.h"

using namespace kal;

TEST ("step input: the numbers' lengths, 1/128 to 1/1; 9 dotted, 0 triplet, for the next")
{
    CHECK_EQ (StepInput::valueOf (1), Tick (PPQ / 32));
    CHECK_EQ (StepInput::valueOf (4), Tick (PPQ / 4));
    CHECK_EQ (StepInput::valueOf (6), Tick (PPQ));
    CHECK_EQ (StepInput::valueOf (8), Tick (PPQ * 4));
    StepInput s;
    CHECK_EQ (s.length(), Tick (PPQ / 4));        // 1/16 to begin with
    s.digit (9);
    s.digit (3);
    CHECK_EQ (s.length(), Tick (PPQ / 8 * 3 / 2)); // a dotted 1/32
    CHECK_EQ (s.lengthName(), std::string ("dotted 1/32"));
    s.digit (5);
    CHECK_EQ (s.length(), Tick (PPQ / 2));         // the dot was for one only
    s.digit (0);
    s.digit (6);
    CHECK_EQ (s.length(), Tick (PPQ * 2 / 3));
    CHECK_EQ (s.lengthName(), std::string ("1/4 triplet"));
}

TEST ("step input: a chord is written when its last key lets go, at the length chosen while it was held")
{
    StepInput s;
    s.keyDown (60, 100);
    s.keyDown (64, 90);
    s.digit (5);
    s.keyDown (67, 80);
    CHECK (s.keyUp (60).empty());       // still holding the others
    CHECK (s.keyUp (64).empty());
    const auto notes = s.keyUp (67);
    CHECK_EQ (notes.size(), size_t (3));
    for (auto& n : notes) CHECK_EQ (n.length, Tick (PPQ / 2));
    CHECK_EQ (notes[1].velocity, 90);
    CHECK (! s.holding());
    // The next chord keeps the length.
    s.keyDown (62, 100);
    const auto next = s.keyUp (62);
    CHECK_EQ (next.size(), size_t (1));
    CHECK_EQ (next[0].length, Tick (PPQ / 2));
    CHECK (s.keyUp (62).empty());       // a key-up with nothing held writes nothing
}
