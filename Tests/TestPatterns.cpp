#include "Check.h"
#include "Patterns.h"
#include "ProjectFile.h"
#include "Edit.h"

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
    writeGrid (m, g, 0, PPQ * 8, id, NoteFrom::Drums);
    CHECK_EQ (m.notes.size(), size_t (9));
    const auto back = readGrid (m, g, 0, NoteFrom::Drums);
    auto k2 = std::find_if (back.lanes.begin(), back.lanes.end(), [] (const Lane& l) { return l.pitch == 36; });
    CHECK_EQ (k2->velocity[0], 110);
    CHECK_EQ (k2->velocity[4], 110);
    CHECK_EQ (k2->velocity[1], 0);
    // Writing again replaces rather than doubles.
    writeGrid (m, g, 0, PPQ * 8, id, NoteFrom::Drums);
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
    writeGrid (m, g, 0, g.stepTicks * 2, id, NoteFrom::Drums);
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
    writeSteps (m, s, 0, s.stepTicks * 4, id, NoteFrom::Steps);
    CHECK_EQ (m.notes.size(), size_t (2));
    CHECK_EQ (m.notes[0].length, s.stepTicks + s.stepTicks / 2);
    const auto back = readSteps (m, s.stepTicks, 4, 0, NoteFrom::Steps);
    CHECK (back.steps[0].on && back.steps[0].tie);
    CHECK (back.steps[1].on && ! back.steps[1].tie);
    CHECK (! back.steps[2].on);
    CHECK_EQ (back.steps[3].pitch, 67);
}

TEST ("one clip, several ways in: each editor shows and changes only its own notes; the piano roll's stay")
{
    MidiData m;
    Note played { 99, 0, PPQ * 2, 48, 90 };   // drawn in the piano roll: a long C under everything
    m.notes.push_back (played);
    unsigned id = 1;

    // The step sequencer starts empty though the clip isn't.
    auto s = readSteps (m, PPQ / 4, 8, 0, NoteFrom::Steps);
    for (auto& st : s.steps) CHECK (! st.on);
    s.steps[0] = { true, 60, 100, 0.5, false };
    s.steps[4] = { true, 64, 100, 0.5, false };
    writeSteps (m, s, 0, PPQ * 2, id, NoteFrom::Steps);
    CHECK_EQ (m.notes.size(), size_t (3));
    CHECK (std::find (m.notes.begin(), m.notes.end(), played) != m.notes.end());

    // The drum grid neither sees the steps nor touches them.
    Grid g;
    g.steps = 8;
    g.lanes = standardKit (8);
    auto drums = readGrid (m, g, 0, NoteFrom::Drums);
    CHECK_EQ (drums.lanes.size(), g.lanes.size());   // no rows made for the others' pitches
    for (auto& l : drums.lanes) for (int v : l.velocity) CHECK_EQ (v, 0);
    drums.lanes[0].velocity[0] = 100;
    writeGrid (m, drums, 0, PPQ * 2, id, NoteFrom::Drums);
    CHECK_EQ (m.notes.size(), size_t (4));

    // Steps written again: theirs replaced, the drum and the piano roll's notes kept.
    s.steps[4].on = false;
    writeSteps (m, s, 0, PPQ * 2, id, NoteFrom::Steps);
    CHECK_EQ (m.notes.size(), size_t (3));
    CHECK (std::find (m.notes.begin(), m.notes.end(), played) != m.notes.end());
    const auto back = readSteps (m, PPQ / 4, 8, 0, NoteFrom::Steps);
    CHECK (back.steps[0].on && ! back.steps[4].on);
    int fromDrums = 0;
    for (auto& n : m.notes) if (n.from == NoteFrom::Drums) ++fromDrums;
    CHECK_EQ (fromDrums, 1);

    // Saved and opened again, each note still knows where it came from.
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Keys");
    const Id clip = edit::addMidiClip (p, t, 0, PPQ * 2);
    p.clip (clip)->midi = m;
    auto loaded = file::load (file::save (p));
    CHECK (loaded.has_value());
    if (loaded) CHECK (loaded->clip (clip)->midi.notes == m.notes);
}
