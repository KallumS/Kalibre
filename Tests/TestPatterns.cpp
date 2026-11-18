#include "Check.h"
#include "Patterns.h"
#include "ProjectFile.h"
#include "Edit.h"
#include "Playback.h"

#include <cmath>

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

TEST ("chance, velocity and timing: the drum grid and the step sequencer keep each step's, through writing and reading")
{
    Grid g;
    g.steps = 4;
    g.lanes = { { "Hat", 42, { 100, 0, 90, 0 } } };
    g.lanes[0].chance = { 50, 100, 75, 100 };
    g.lanes[0].velocityRange = { 10, 0, 0, 0 };
    g.lanes[0].timingMs = { 0, 0, 15, 0 };
    MidiData m;
    unsigned id = 1;
    writeGrid (m, g, 0, g.stepTicks * 4, id, NoteFrom::Drums);
    CHECK_EQ (m.notes.size(), size_t (2));
    CHECK_EQ (m.notes[0].chance, 50);
    CHECK_EQ (m.notes[0].velocityRange, 10);
    CHECK_EQ (m.notes[1].timingMs, 15.0);
    const auto back = readGrid (m, g, 0, NoteFrom::Drums);
    CHECK_EQ (back.lanes[0].chance[0], 50);
    CHECK_EQ (back.lanes[0].chance[2], 75);
    CHECK_EQ (back.lanes[0].velocityRange[0], 10);
    CHECK_EQ (back.lanes[0].timingMs[2], 15.0);

    StepSequence s;
    s.steps.assign (4, {});
    s.steps[1].on = true;
    s.steps[1].chance = 25;
    s.steps[1].velocityRange = 8;
    s.steps[1].timingMs = 5;
    MidiData m2;
    writeSteps (m2, s, 0, s.stepTicks * 4, id, NoteFrom::Steps);
    CHECK_EQ (m2.notes.size(), size_t (1));
    CHECK_EQ (m2.notes[0].chance, 25);
    const auto sb = readSteps (m2, s.stepTicks, 4, 0, NoteFrom::Steps);
    CHECK_EQ (sb.steps[1].chance, 25);
    CHECK_EQ (sb.steps[1].velocityRange, 8);
    CHECK_EQ (sb.steps[1].timingMs, 5.0);
}

TEST ("random: Euclidean settings that make sense, a different set each time")
{
    int differ = 0;
    EuclidSettings last;
    for (unsigned seed = 1; seed <= 200; ++seed)
    {
        for (int pitch : { 36, 38, 42, 39 })
        {
            const auto e = randomEuclid (pitch, seed);
            CHECK (e.steps >= 4 && e.steps <= 32);
            CHECK (e.pulses >= 1 && e.pulses < e.steps);
            CHECK (e.rotation >= 0 && e.rotation < e.steps);
        }
        const auto e = randomEuclid (42, seed);
        if (seed > 1 && ! (e.steps == last.steps && e.pulses == last.pulses && e.rotation == last.rotation)) ++differ;
        last = e;
    }
    CHECK (differ > 150);
    // Hats busier than kicks, on the whole.
    int hats = 0, kicks = 0;
    for (unsigned seed = 1; seed <= 200; ++seed) { hats += randomEuclid (42, seed).pulses; kicks += randomEuclid (36, seed).pulses; }
    CHECK (hats > kicks);
}

TEST ("drum grid: a drum the usual kit hasn't got gets a row with its General MIDI name")
{
    MidiData m;
    Note n; n.start = 0; n.length = 60; n.pitch = 44; n.from = NoteFrom::Drums;
    m.notes.push_back (n);
    Grid g;
    g.lanes = standardKit (16);
    const auto r = readGrid (m, g, 0, NoteFrom::Drums);
    CHECK_EQ (r.lanes.back().name, std::string ("Pedal Hi-Hat"));
    CHECK_EQ (drumName (56), std::string ("Cowbell"));
}

//==============================================================================
// Swing, gate and repeat (decision 0063).
TEST ("drum grid: a hit repeated three times is three notes in its step, read back as one hit repeated three times")
{
    Grid g;
    g.steps = 4;
    g.gate = 0.5;
    g.lanes = { { "Snare", 38, { 100, 0, 0, 0 }, {}, {}, {}, { 3, 1, 1, 1 } } };
    MidiData m;
    unsigned id = 1;
    writeGrid (m, g, 0, g.stepTicks * 4, id, NoteFrom::Drums);
    CHECK_EQ (m.notes.size(), size_t (3));
    const Tick third = g.stepTicks / 3;
    for (int k = 0; k < 3; ++k)
    {
        CHECK_EQ (m.notes[(size_t) k].start, third * k);
        CHECK_EQ (m.notes[(size_t) k].length, third / 2);            // the gate, of each repeat
        CHECK_EQ (m.notes[(size_t) k].repeat, k == 0 ? 3 : 0);       // the first says how many; the rest are its repeats
    }
    const auto back = readGrid (m, g, 0, NoteFrom::Drums);
    CHECK_EQ (back.lanes[0].velocity[0], 100);
    CHECK_EQ (back.lanes[0].repeat[0], 3);
    CHECK_EQ (back.lanes[0].velocity[1], 0);                        // the repeats aren't hits of their own
}

TEST ("drum grid: swung hits are read back onto their own steps, however far they're pushed")
{
    Grid g;
    g.steps = 4;
    g.swing = 0.7;    // the off-steps pushed 70% of a step late
    g.lanes = { { "Hat", 42, { 100, 100, 100, 100 } } };
    MidiData m;
    unsigned id = 1;
    writeGrid (m, g, 0, g.stepTicks * 4, id, NoteFrom::Drums);
    CHECK_EQ (m.notes[1].start, g.stepTicks + (Tick) std::lround (0.7 * g.stepTicks));
    const auto back = readGrid (m, g, 0, NoteFrom::Drums);
    for (int s = 0; s < 4; ++s) CHECK_EQ (back.lanes[0].velocity[(size_t) s], 100);
}

TEST ("steps: a repeated step is its notes within the step; swing and the gate as written; all read back")
{
    StepSequence s;
    s.steps.assign (4, {});
    s.swing = 0.5;
    s.steps[1] = { true, 64, 100, 0.8, false };
    s.steps[1].repeat = 2;
    s.steps[2] = { true, 67, 100, 0.5, false };
    MidiData m;
    unsigned id = 1;
    writeSteps (m, s, 0, s.stepTicks * 4, id, NoteFrom::Steps);
    CHECK_EQ (m.notes.size(), size_t (3));
    const Tick swung = s.stepTicks + s.stepTicks / 2;
    CHECK_EQ (m.notes[0].start, swung);
    CHECK_EQ (m.notes[1].start, swung + s.stepTicks / 2);
    CHECK_EQ (m.notes[0].length, (Tick) std::lround (0.8 * (s.stepTicks / 2)));
    CHECK_EQ (m.notes[0].repeat, 2);
    CHECK_EQ (m.notes[1].repeat, 0);
    const auto back = readSteps (m, s.stepTicks, 4, 0, NoteFrom::Steps);
    CHECK (back.steps[1].on && back.steps[1].repeat == 2 && back.steps[1].pitch == 64);
    CHECK (std::abs (back.steps[1].gate - 0.8) < 0.02);
    CHECK (back.steps[2].on && back.steps[2].repeat == 1 && std::abs (back.steps[2].gate - 0.5) < 0.02);
    CHECK (! back.steps[3].on);
}

TEST ("feel: each editor's swing and gate are kept with the clip")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Beat");
    const Id c = edit::addMidiClip (p, t, 0, PPQ * 4);
    CHECK_EQ (p.clip (c)->drumsFeel.gate, 0.9);
    CHECK_EQ (p.clip (c)->stepsFeel.gate, 0.5);
    p.clip (c)->drumsFeel = { 0.3, 0.6 };
    p.clip (c)->stepsFeel = { 0.2, 0.25 };
    p.clip (c)->euclidFeel = { 0.55, 0.4 };
    auto loaded = file::load (file::save (p));
    CHECK (loaded && loaded->clip (c)->drumsFeel == p.clip (c)->drumsFeel);
    CHECK (loaded && loaded->clip (c)->stepsFeel == p.clip (c)->stepsFeel);
    CHECK (loaded && loaded->clip (c)->euclidFeel == p.clip (c)->euclidFeel);
}

TEST ("looped clips: Loop Its Content on a clip split from another loops what it plays, and the editors write into that loop")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Keys");
    const Id left = edit::addMidiClip (p, t, 0, PPQ * 8);
    Note n; n.start = PPQ * 5; n.length = PPQ / 2; n.pitch = 60;
    edit::addNote (p, left, n);
    const Id right = edit::splitClip (p, left, PPQ * 4);   // the right half starts 4 beats into its content
    CHECK (p.clip (right)->offset == PPQ * 4);

    edit::loopContent (p, right);
    const auto* c = p.clip (right);
    CHECK_EQ (c->offset, Tick (0));
    CHECK_EQ (c->loopLength, Tick (PPQ * 4));
    CHECK_EQ (c->length, Tick (PPQ * 8));
    CHECK_EQ (c->midi.notes.size(), size_t (1));
    CHECK_EQ (c->midi.notes.front().start, Tick (PPQ));       // where it played, still
    const auto w = windowOf (*c);
    CHECK_EQ (w.from, Tick (0));
    CHECK_EQ (w.length, Tick (PPQ * 4));

    // The step sequencer's notes, written into the window, play on both passes.
    StepSequence s = readSteps (c->midi, PPQ, 4, w.from, NoteFrom::Steps);
    s.steps[0].on = true;
    s.steps[0].pitch = 64;
    unsigned next = 1000;
    writeSteps (p.clip (right)->midi, s, w.from, w.length, next, NoteFrom::Steps);
    int plays = 0;
    const auto compiled = playback::compile (p, *p.track (t));
    for (auto& m : compiled.midi)
        if ((m.bytes[0] & 0xF0) == 0x90 && m.bytes[1] == 64 && m.bytes[2] > 0) ++plays;
    CHECK_EQ (plays, 2);

    // Looped again, it's let go; a clip already looped with an offset (saved before) still has its window from 0.
    edit::loopContent (p, right);
    CHECK_EQ (p.clip (right)->loopLength, Tick (0));
    Clip old; old.kind = Clip::Kind::Midi; old.offset = PPQ; old.loopLength = PPQ * 4; old.length = PPQ * 8;
    CHECK_EQ (windowOf (old).from, Tick (0));
    CHECK_EQ (windowOf (old).length, Tick (PPQ * 4));
    Clip plain; plain.kind = Clip::Kind::Midi; plain.offset = PPQ; plain.length = PPQ * 2;
    CHECK_EQ (windowOf (plain).from, Tick (PPQ));
    CHECK_EQ (windowOf (plain).length, Tick (PPQ * 2));
}
