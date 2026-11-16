#include "Check.h"
#include "Roll.h"

using namespace kal;
using namespace kal::roll;

static Settings settings()
{
    Settings s;
    s.edgeTicks = 40;
    s.defaultLength = PPQ / 4;
    s.minLength = PPQ / 4;
    s.snap = [] (Tick t) { return (t + PPQ / 8) / (PPQ / 4) * (PPQ / 4); };
    s.snapDown = [] (Tick t) { return t / (PPQ / 4) * (PPQ / 4); };
    return s;
}

static std::vector<Note> twoNotes()
{
    return { { 1, 0, PPQ, 60, 100 }, { 2, PPQ * 2, PPQ, 64, 100 } };
}

TEST ("roll: zones - edges are a third at most")
{
    const Note n { 1, 0, 90, 60, 100 };
    CHECK (zoneOf (n, 5, 60, 40) == Zone::StartEdge);
    CHECK (zoneOf (n, 45, 60, 40) == Zone::Body);
    CHECK (zoneOf (n, 85, 60, 40) == Zone::EndEdge);
    CHECK (zoneOf (n, 45, 61, 40) == Zone::Empty);
}

TEST ("roll: one left click in empty space adds a note at the grid cell")
{
    auto notes = twoNotes();
    Gesture g (notes, {}, settings());
    auto o = g.press ({ PPQ + 300, 67 });
    CHECK (o.action == Action::AddNote);
    CHECK_EQ (o.added.start, PPQ + 240);
    CHECK_EQ (o.added.pitch, 67);
    CHECK (o.audition);
    o = g.release ({ PPQ + 300, 67 });
    CHECK (o.action == Action::AddNote);
    CHECK_EQ (o.added.length, PPQ / 4);
}

TEST ("roll: dragging after adding draws the note longer")
{
    auto notes = twoNotes();
    Gesture g (notes, {}, settings());
    g.press ({ PPQ, 67 });
    g.drag ({ PPQ * 2 - 10, 67 });
    const auto o = g.release ({ PPQ * 2 - 10, 67 });
    CHECK (o.action == Action::AddNote);
    CHECK_EQ (o.added.length, PPQ);
}

TEST ("roll: right click on a note removes it")
{
    auto notes = twoNotes();
    Gesture g (notes, { 1 }, settings());
    g.press ({ PPQ / 2, 60, true });
    const auto o = g.release ({ PPQ / 2, 60, true });
    CHECK (o.action == Action::RemoveNotes);
    CHECK_EQ (o.notes.size(), size_t (1));
    CHECK_EQ (*o.notes.begin(), Id (1));
    CHECK (o.selection.empty());
}

TEST ("roll: right click in empty space does nothing")
{
    auto notes = twoNotes();
    Gesture g (notes, {}, settings());
    g.press ({ PPQ * 6, 70, true });
    CHECK (g.release ({ PPQ * 6, 70, true }).action == Action::None);
}

TEST ("roll: left drag on a note's body moves it, snapped")
{
    auto notes = twoNotes();
    Gesture g (notes, {}, settings());
    auto o = g.press ({ PPQ / 2, 60 });
    CHECK (o.action == Action::Select);
    o = g.drag ({ PPQ / 2 + PPQ + 30, 62 });
    CHECK (o.action == Action::MoveNotes);
    CHECK_EQ (o.dt, PPQ);
    CHECK_EQ (o.dPitch, 2);
    o = g.release ({ PPQ / 2 + PPQ + 30, 62 });
    CHECK (o.action == Action::MoveNotes);
    CHECK_EQ (o.notes, std::set<Id> ({ 1 }));
}

TEST ("roll: moving one of the chosen notes moves them all; Option copies")
{
    auto notes = twoNotes();
    Gesture g (notes, { 1, 2 }, settings());
    g.press ({ PPQ / 2, 60, false, false, true });
    g.drag ({ PPQ / 2 + PPQ, 60, false, false, true });
    const auto o = g.release ({ PPQ / 2 + PPQ, 60, false, false, true });
    CHECK (o.action == Action::CopyNotes);
    CHECK_EQ (o.notes.size(), size_t (2));
}

TEST ("roll: left drag on the end edge stretches and shortens")
{
    auto notes = twoNotes();
    Gesture g (notes, {}, settings());
    g.press ({ PPQ - 10, 60 });
    CHECK (g.mode() == Gesture::Mode::ResizingEnd);
    auto o = g.drag ({ PPQ * 2 - 10, 60 });
    CHECK (o.action == Action::ResizeNotes);
    CHECK_EQ (o.dEnd, PPQ);
    // Shortening never goes below the grid step.
    o = g.drag ({ -PPQ * 4, 60 });
    CHECK_EQ (o.dEnd, PPQ / 4 - PPQ);
}

TEST ("roll: left drag on the start edge moves the start, keeping the end")
{
    auto notes = twoNotes();
    Gesture g (notes, {}, settings());
    g.press ({ PPQ * 2 + 5, 64 });
    CHECK (g.mode() == Gesture::Mode::ResizingStart);
    const auto o = g.drag ({ PPQ * 2 + PPQ / 2, 64 });
    CHECK_EQ (o.dStart, PPQ / 2);
    CHECK_EQ (o.dEnd, Tick (0));
}

TEST ("roll: right drag over an area removes every note in it")
{
    auto notes = twoNotes();
    Gesture g (notes, {}, settings());
    g.press ({ 0, 70, true });
    auto o = g.drag ({ PPQ * 3, 50, true });
    CHECK (o.showArea);
    o = g.release ({ PPQ * 3, 50, true });
    CHECK (o.action == Action::RemoveNotes);
    CHECK_EQ (o.notes.size(), size_t (2));
}

TEST ("roll: Shift and left drag chooses the notes in an area")
{
    auto notes = twoNotes();
    Gesture g (notes, {}, settings());
    g.press ({ PPQ * 3 / 2, 70, false, true });
    g.drag ({ PPQ * 4, 62, false, true });
    const auto o = g.release ({ PPQ * 4, 62, false, true });
    CHECK (o.action == Action::Select);
    CHECK_EQ (o.selection, std::set<Id> ({ 2 }));
}

TEST ("roll: Shift-click chooses every note from the last clicked; Option-click adds; Shift-Option-click takes out")
{
    // Four notes: 60 at beat 1, 62 at beat 2, 64 at beat 3, and 72 at beat 2 (above the range).
    std::vector<Note> notes { { 1, 0, PPQ, 60, 100 }, { 2, PPQ, PPQ, 62, 100 }, { 3, PPQ * 2, PPQ, 64, 100 }, { 4, PPQ, PPQ, 72, 100 } };
    const Pointer shift { 0, 0, false, true }, option { 0, 0, false, false, true }, both { 0, 0, false, true, true };
    auto at = [] (Pointer p, Tick t, int pitch) { p.tick = t; p.pitch = pitch; return p; };

    // Click the first, Shift-click the third: the second comes too, the high one doesn't.
    Gesture g (notes, {}, settings());
    g.press ({ PPQ / 2, 60 });
    auto o = g.release ({ PPQ / 2, 60 });
    CHECK_EQ (g.anchor(), Id (1));
    Gesture h (notes, o.selection, settings(), g.anchor());
    h.press (at (shift, PPQ * 2 + PPQ / 2, 64));
    o = h.release (at (shift, PPQ * 2 + PPQ / 2, 64));
    CHECK_EQ (o.selection, std::set<Id> ({ 1, 2, 3 }));

    // Option-click adds the high one.
    Gesture k (notes, o.selection, settings(), h.anchor());
    k.press (at (option, PPQ + PPQ / 2, 72));
    o = k.release (at (option, PPQ + PPQ / 2, 72));
    CHECK_EQ (o.selection, std::set<Id> ({ 1, 2, 3, 4 }));

    // Shift-Option-click takes the second out.
    Gesture m (notes, o.selection, settings(), k.anchor());
    m.press (at (both, PPQ + PPQ / 2, 62));
    o = m.release (at (both, PPQ + PPQ / 2, 62));
    CHECK_EQ (o.selection, std::set<Id> ({ 1, 3, 4 }));

    // Shift-Option-drag takes an area out.
    Gesture q (notes, o.selection, settings(), m.anchor());
    q.press (at (both, PPQ * 2 - 10, 70));
    q.drag (at (both, PPQ * 3, 63));
    o = q.release (at (both, PPQ * 3, 63));
    CHECK_EQ (o.selection, std::set<Id> ({ 1, 4 }));

    // Option-drag still copies, the added note with the rest.
    Gesture r (notes, { 1 }, settings(), 1);
    r.press (at (option, PPQ * 2 + PPQ / 2, 64));
    o = r.drag (at (option, PPQ * 3 + PPQ / 2, 64));
    CHECK (o.action == Action::CopyNotes);
    CHECK_EQ (o.notes, std::set<Id> ({ 1, 3 }));
}

TEST ("roll: Command drags off the grid")
{
    auto notes = twoNotes();
    Gesture g (notes, {}, settings());
    g.press ({ PPQ / 2, 60 });
    const auto o = g.drag ({ PPQ / 2 + 37, 60, false, false, false, true });
    CHECK_EQ (o.dt, Tick (37));
}

TEST ("roll: a new note takes the length of the last note added, clicked or stretched")
{
    // A 1/8 note and a 1/16 note.
    std::vector<Note> notes { { 1, 0, PPQ / 2, 60, 100 }, { 2, PPQ * 2, PPQ / 4, 64, 100 } };

    // Drawn out to an 1/8: that's what the next one gets.
    Gesture g (notes, {}, settings());
    g.press ({ PPQ * 4, 67 });
    g.drag ({ PPQ * 4 + PPQ / 2, 67 });
    auto o = g.release ({ PPQ * 4 + PPQ / 2, 67 });
    CHECK (o.action == Action::AddNote);
    CHECK_EQ (o.noteLength, Tick (PPQ / 2));

    // Clicking the 1/16 note: the next is a 1/16.
    Gesture h (notes, {}, settings());
    h.press ({ PPQ * 2 + PPQ / 8, 64 });
    o = h.release ({ PPQ * 2 + PPQ / 8, 64 });
    CHECK_EQ (o.noteLength, Tick (PPQ / 4));

    // Stretching the 1/8 to a 1/4: the next is a 1/4.
    Gesture k (notes, {}, settings());
    k.press ({ PPQ / 2 - 5, 60 });
    k.drag ({ PPQ - 5, 60 });
    o = k.release ({ PPQ - 5, 60 });
    CHECK (o.action == Action::ResizeNotes);
    CHECK_EQ (o.noteLength, Tick (PPQ));

    // A plain click in empty space adds a note of that length (the window passes it as the default).
    auto s = settings();
    s.defaultLength = PPQ;
    Gesture m (notes, {}, s);
    o = m.press ({ PPQ * 8, 70 });
    CHECK_EQ (o.added.length, Tick (PPQ));
    o = m.release ({ PPQ * 8, 70 });
    CHECK_EQ (o.noteLength, Tick (PPQ));
}
