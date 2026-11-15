#include "Check.h"
#include "Patterns.h"

using namespace kal;
using namespace kal::patterns;

static std::string text (const std::vector<bool>& v)
{
    std::string s;
    for (bool b : v) s += b ? 'x' : '.';
    return s;
}

TEST ("euclid: the well-known rhythms")
{
    CHECK_EQ (text (euclid (8, 3)), std::string ("x..x..x."));
    CHECK_EQ (text (euclid (8, 5)), std::string ("x.xx.xx."));
    CHECK_EQ (text (euclid (16, 4)), std::string ("x...x...x...x..."));
    CHECK_EQ (text (euclid (4, 0)), std::string ("...."));
    CHECK_EQ (text (euclid (4, 9)), std::string ("xxxx"));
    CHECK_EQ (text (euclid (8, 3, 1)), std::string (".x..x..x"));
}

TEST ("drum grid: write then read back")
{
    Grid g;
    g.lanes = standardKit (16);
    auto kick = std::find_if (g.lanes.begin(), g.lanes.end(), [] (const Lane& l) { return l.pitch == 36; });
    euclidLane (*kick, 16, 4, 0, 110);
    MidiData m;
    m.notes.push_back ({ 99, 10, PPQ, 72, 90 });   // a melody note, left alone
    unsigned id = 1;
    writeGrid (m, g, 0, PPQ * 8, id);
    CHECK_EQ (m.notes.size(), size_t (9));
    const auto back = readGrid (m, g, 0);
    auto k2 = std::find_if (back.lanes.begin(), back.lanes.end(), [] (const Lane& l) { return l.pitch == 36; });
    CHECK_EQ (k2->velocity[0], 110);
    CHECK_EQ (k2->velocity[4], 110);
    CHECK_EQ (k2->velocity[1], 0);
    // Writing again replaces rather than doubles.
    writeGrid (m, g, 0, PPQ * 8, id);
    CHECK_EQ (m.notes.size(), size_t (9));
}

TEST ("drum grid: swing pushes the off-steps")
{
    Grid g;
    g.steps = 2;
    g.swing = 0.5;
    g.lanes = { { "Hat", 42, { 100, 100 } } };
    MidiData m;
    unsigned id = 1;
    writeGrid (m, g, 0, g.stepTicks * 2, id);
    CHECK_EQ (m.notes[1].start, g.stepTicks + g.stepTicks / 2);
}

TEST ("steps: ties make one long note; reading gives the steps back")
{
    StepSequence s;
    s.steps.assign (4, {});
    s.steps[0] = { true, 60, 100, 0.5, true };
    s.steps[1] = { true, 60, 100, 0.5, false };
    s.steps[3] = { true, 67, 90, 0.25, false };
    MidiData m;
    unsigned id = 1;
    writeSteps (m, s, 0, s.stepTicks * 4, id);
    CHECK_EQ (m.notes.size(), size_t (2));
    CHECK_EQ (m.notes[0].length, s.stepTicks + s.stepTicks / 2);
    const auto back = readSteps (m, s.stepTicks, 4);
    CHECK (back.steps[0].on && back.steps[0].tie);
    CHECK (back.steps[1].on && ! back.steps[1].tie);
    CHECK (! back.steps[2].on);
    CHECK_EQ (back.steps[3].pitch, 67);
}
