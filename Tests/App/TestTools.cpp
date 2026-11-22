// The toolbox (decision 0092): Control-right-click opens it over the arrangement
// and the piano roll; each tool, driven with the mouse, is one undo step.
#include "Check.h"
#include "Arrange.h"
#include "Controller.h"
#include "Edit.h"
#include "Engine.h"
#include "PianoRoll.h"
#include "PluginManager.h"
#include "ToolPicker.h"

using namespace kal;
using namespace juce;
using tools::Tool;

namespace
{
const ModifierKeys left { ModifierKeys::leftButtonModifier };

MouseEvent event (Component& comp, Point<int> p, ModifierKeys mods, Point<int> down, bool dragged)
{
    return MouseEvent (Desktop::getInstance().getMainMouseSource(), p.toFloat(), mods, MouseInputSource::defaultPressure,
                       0.0f, 0.0f, 0.0f, 0.0f, &comp, &comp, Time::getCurrentTime(), down.toFloat(), Time::getCurrentTime(), 1, dragged);
}

void click (Component& comp, Point<int> p, ModifierKeys mods = left)
{
    comp.mouseDown (event (comp, p, mods, p, false));
    comp.mouseUp (event (comp, p, mods, p, false));
}

// `moves`: how many mouse events the drag is; a quick drag is only one or two.
void drag (Component& comp, Point<int> from, Point<int> to, ModifierKeys mods = left, int moves = 8)
{
    comp.mouseDown (event (comp, from, mods, from, false));
    for (int i = 1; i <= moves; ++i)
    {
        const auto p = from + (to - from) * i / moves;
        comp.mouseDrag (event (comp, p, mods, from, true));
    }
    comp.mouseUp (event (comp, to, mods, from, true));
}

ToolBox* openToolBox()
{
    for (int i = 0; i < Desktop::getInstance().getNumComponents(); ++i)
        if (auto* b = dynamic_cast<ToolBox*> (Desktop::getInstance().getComponent (i))) return b;
    return nullptr;
}

struct Song
{
    PluginManager plugins;
    Engine engine { plugins };
    Controller c { engine };
    Id one = 0, two = 0;
    Song()
    {
        c.project = Project();
        one = edit::addTrack (c.project, TrackKind::Track, "One");
        two = edit::addTrack (c.project, TrackKind::Track, "Two");
        c.refresh();
    }
};

struct ArrangeView
{
    Arrange view;
    explicit ArrangeView (Controller& c) : view (c)
    {
        view.setSize (1400, 600);
        view.timeline.startTick = 0;
        view.timeline.pixelsPerTick = 100.0 / PPQ;   // a beat is 100 points
        c.refresh();
        MessageManager::getInstance()->runDispatchLoopUntil (20);
    }
    // The middle of a track's lane (the tracks are 64 points tall, first to last).
    Point<int> at (Tick t, int track) const
    {
        return { view.headerWidth() + roundToInt (view.timeline.x (t)), view.rulerHeight() + track * theme::px (64) + theme::px (32) };
    }
};
} // namespace

TEST ("tools: Control-right-click opens the toolbox over the arrangement; a click in it picks the tool")
{
    Song s;
    ArrangeView a (s.c);
    // Control-click alone is still the Mac's right-click: no toolbox.
    CHECK (openToolBox() == nullptr);
    a.view.mouseDown (event (a.view, a.at (PPQ * 20, 1), ModifierKeys (ModifierKeys::ctrlModifier | ModifierKeys::rightButtonModifier), a.at (PPQ * 20, 1), false));
    auto* box = openToolBox();
    CHECK (box != nullptr);
    if (! box) return;
    // The arrangement's tools: no Drumstick; Time Warp there.
    CHECK_EQ (box->shown.size(), size_t (12));
    CHECK (std::find (box->shown.begin(), box->shown.end(), Tool::Drumstick) == box->shown.end());
    // Clicking Split picks it, and the toolbox goes.
    const int split = (int) (std::find (box->shown.begin(), box->shown.end(), Tool::Split) - box->shown.begin());
    const auto p = box->cellBounds (split).getCentre();
    box->mouseUp (event (*box, p, left, p, false));
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK (s.c.arrangeTool.tool == Tool::Split);
    CHECK (openToolBox() == nullptr);
    CHECK (s.c.message.contains ("Split"));
    // The piano roll keeps its own tool.
    CHECK (s.c.rollTool.tool == Tool::Select);
}

TEST ("tools: Split, Glue, Mute and Erase in the arrangement, each one undo step")
{
    Song s;
    auto& c = s.c;
    const Id clip = edit::addMidiClip (c.project, s.one, 0, PPQ * 8);
    edit::addMidiClip (c.project, s.one, PPQ * 12, PPQ * 4);
    c.arrangeGrid = PPQ;
    c.refresh();
    ArrangeView a (c);

    // Split: a click cuts the clip at the grid line nearest it.
    c.chooseTool (tools::Area::Arrange, Tool::Split);
    auto steps = c.history.size();
    click (a.view, a.at (PPQ * 3 + 20, 0));
    CHECK_EQ (c.project.track (s.one)->clips().size(), size_t (3));
    CHECK_EQ (c.project.clip (clip)->length, PPQ * 3);
    CHECK_EQ (c.history.size(), steps + 1);

    // Glue: the first piece clicked joins the next.
    c.chooseTool (tools::Area::Arrange, Tool::Glue);
    steps = c.history.size();
    click (a.view, a.at (PPQ, 0));
    CHECK_EQ (c.project.track (s.one)->clips().size(), size_t (2));
    CHECK_EQ (c.history.size(), steps + 1);

    // Mute: a click mutes it; again unmutes.
    c.chooseTool (tools::Area::Arrange, Tool::Mute);
    steps = c.history.size();
    click (a.view, a.at (PPQ, 0));
    const auto& clips = c.project.track (s.one)->clips();
    CHECK (clips.front().muted);
    CHECK_EQ (c.history.size(), steps + 1);
    click (a.view, a.at (PPQ, 0));
    CHECK (! c.project.track (s.one)->clips().front().muted);

    // Erase: one drag over both clips takes both, one undo step.
    c.chooseTool (tools::Area::Arrange, Tool::Erase);
    steps = c.history.size();
    drag (a.view, a.at (PPQ, 0), a.at (PPQ * 14, 0), left, 1);   // quick: one move, both still erased
    CHECK (c.project.track (s.one)->clips().empty());
    CHECK_EQ (c.history.size(), steps + 1);
    CHECK (c.undo());
    CHECK_EQ (c.project.track (s.one)->clips().size(), size_t (2));
}

TEST ("tools: Draw makes a clip as long as the drag; Range clears a stretch on the tracks dragged over")
{
    Song s;
    auto& c = s.c;
    c.arrangeGrid = PPQ;
    edit::addMidiClip (c.project, s.one, 0, PPQ * 8);
    edit::addMidiClip (c.project, s.two, 0, PPQ * 8);
    c.refresh();
    ArrangeView a (c);

    c.chooseTool (tools::Area::Arrange, Tool::Draw);
    drag (a.view, a.at (PPQ * 10, 1), a.at (PPQ * 13, 1));
    const auto& two = c.project.track (s.two)->clips();
    CHECK_EQ (two.size(), size_t (2));
    CHECK_EQ (two.back().start, PPQ * 10);
    CHECK_EQ (two.back().length, PPQ * 3);

    c.chooseTool (tools::Area::Arrange, Tool::Range);
    drag (a.view, a.at (PPQ * 2, 0), a.at (PPQ * 4, 1));
    CHECK_EQ (c.range.tracks.size(), size_t (2));
    CHECK_EQ (c.range.from, PPQ * 2);
    CHECK_EQ (c.range.to, PPQ * 4);
    const auto steps = c.history.size();
    c.deleteSelection();
    CHECK_EQ (c.history.size(), steps + 1);
    CHECK (c.range.empty());
    for (auto t : { s.one, s.two })
    {
        const auto& cl = c.project.track (t)->clips();
        CHECK (cl.size() >= 2);
        CHECK_EQ (cl[0].end(), PPQ * 2);
        CHECK_EQ (cl[1].start, PPQ * 4);
    }
}

TEST ("tools: Time Warp moves a beat to where it's let go; Zoom and Hand only change the view")
{
    Song s;
    auto& c = s.c;
    c.arrangeGrid = PPQ;
    ArrangeView a (c);
    c.chooseTool (tools::Area::Arrange, Tool::TimeWarp);
    const auto steps = c.history.size();
    // Bar 3 dragged half a beat later: the two bars before it slow down.
    drag (a.view, a.at (PPQ * 8, 0), a.at (PPQ * 8 + PPQ / 2, 0));
    CHECK_EQ (c.history.size(), steps + 1);
    CHECK (std::abs (c.project.tempo.secondsAt (PPQ * 8) - 4.25) < 0.01);
    CHECK (std::abs (c.project.tempo.bpmAt (PPQ * 8) - 120.0) < 1e-6);

    c.chooseTool (tools::Area::Arrange, Tool::Zoom);
    const double before = a.view.timeline.pixelsPerTick;
    click (a.view, a.at (PPQ * 4, 0));
    CHECK (std::abs (a.view.timeline.pixelsPerTick - before * 2) < 1e-9);
    click (a.view, a.at (PPQ * 4, 0), ModifierKeys (ModifierKeys::leftButtonModifier | ModifierKeys::altModifier));
    CHECK (std::abs (a.view.timeline.pixelsPerTick - before) < 1e-9);

    c.chooseTool (tools::Area::Arrange, Tool::Hand);
    a.view.timeline.startTick = PPQ * 8;
    drag (a.view, a.at (PPQ * 10, 0), a.at (PPQ * 10, 0) + Point<int> (100, 0));
    CHECK (std::abs (a.view.timeline.startTick - PPQ * 7) < 2);
    CHECK_EQ (c.history.size(), steps + 1);
}

TEST ("tools: in the piano roll, Draw, Erase, Mute, Split, Drumstick and Line; Select keeps decision 0009")
{
    Song s;
    auto& c = s.c;
    const Id clip = edit::addMidiClip (c.project, s.one, 0, PPQ * 8);
    const Id held = edit::addNote (c.project, clip, { 0, 0, PPQ * 2, 60 });
    c.grid = PPQ / 2;
    c.refresh();
    c.selectClip (clip);
    PianoRoll roll (c);
    roll.setSize (1200, 700);
    roll.timeline.startTick = 0;
    roll.timeline.pixelsPerTick = 100.0 / PPQ;
    roll.rowHeight = 12;
    roll.scrollPitch = 72;
    auto at = [&] (Tick t, int pitch)
    {
        return Point<int> (theme::px (56) + roundToInt (roll.timeline.x (t)), theme::px (20) + roundToInt ((72.0f - (float) pitch) * 12.0f) + 6);
    };
    auto notes = [&] { return c.project.clip (clip)->midi.notes; };

    // Select (the rules, 0009): a click on a note doesn't add one.
    click (roll, at (PPQ / 2, 60));
    CHECK_EQ (notes().size(), size_t (1));

    // Draw: a note even over the one there.
    c.chooseTool (tools::Area::Roll, Tool::Draw);
    click (roll, at (PPQ / 2 + 10, 60));
    CHECK_EQ (notes().size(), size_t (2));

    // Mute: the held note is kept but muted (where the drawn one, as long as the last clicked, doesn't cover it).
    c.chooseTool (tools::Area::Roll, Tool::Mute);
    click (roll, at (10, 60));
    bool muted = false;
    for (auto& n : notes()) if (n.id == held) muted = n.muted;
    CHECK (muted);

    // Split: the held note cut at the grid line.
    c.chooseTool (tools::Area::Roll, Tool::Split);
    click (roll, at (PPQ / 4 + 10, 60));
    CHECK_EQ (notes().size(), size_t (3));

    // Erase: a drag along the row takes all of them, one undo step.
    c.chooseTool (tools::Area::Roll, Tool::Erase);
    auto steps = c.history.size();
    drag (roll, at (10, 60), at (PPQ * 2 - 10, 60));
    CHECK (notes().empty());
    CHECK_EQ (c.history.size(), steps + 1);

    // Drumstick: a hit every grid step dragged over, one undo step; a click on one takes it away.
    c.chooseTool (tools::Area::Roll, Tool::Drumstick);
    steps = c.history.size();
    drag (roll, at (10, 36), at (PPQ * 2 - 10, 36), left, 1);   // quick: one move, every step still hit
    CHECK_EQ (notes().size(), size_t (4));
    CHECK_EQ (c.history.size(), steps + 1);
    for (auto& n : notes()) { CHECK_EQ (n.pitch, 36); CHECK_EQ (n.length, PPQ / 2); }
    click (roll, at (10, 36));
    CHECK_EQ (notes().size(), size_t (3));

    // Line: a run of notes climbing from one end to the other.
    c.chooseTool (tools::Area::Roll, Tool::Line);
    drag (roll, at (PPQ * 4 + 10, 60), at (PPQ * 6 + 10, 64));
    int run = 0;
    for (auto& n : notes()) if (n.start >= PPQ * 4) ++run;
    CHECK_EQ (run, 5);
}

#include "MainComponent.h"

TEST ("tools: the number keys choose tools in Cubase's order, in each area that has the tool")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        Song s;
        MainComponent w (s.c);
        s.c.keys.file = File();   // not kept
        s.c.keys.resetAll();
        CHECK (s.c.keys.handle (KeyPress ('3')));
        CHECK (s.c.arrangeTool.tool == Tool::Split);
        CHECK (s.c.rollTool.tool == Tool::Split);
        CHECK (s.c.keys.handle (KeyPress ('8')));
        CHECK (s.c.arrangeTool.tool == Tool::Draw);
        CHECK (s.c.keys.handle (KeyPress ('1')));
        CHECK (s.c.arrangeTool.tool == Tool::Select && s.c.rollTool.tool == Tool::Select);
        // Every tool is in the Keys tab; the Drumstick, given a key, is the piano roll's alone.
        int listed = 0;
        for (auto& k : s.c.keys.all()) listed += k.name.startsWith ("Tool: ") ? 1 : 0;
        CHECK_EQ (listed, tools::toolCount);
        s.c.keys.set ("toolDrumstick", KeyPress ('k'));
        CHECK (s.c.keys.handle (KeyPress ('k')));
        CHECK (s.c.rollTool.tool == Tool::Drumstick);
        CHECK (s.c.arrangeTool.tool == Tool::Select);
        s.c.keys.resetAll();
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("arrange: Shift-drag anywhere on a clip resizes it from its nearer edge, one undo step; a Shift-click only chooses (0095)")
{
    Song s;
    ArrangeView a (s.c);
    s.c.apply ("Clip", [&] (Project& p) { edit::addMidiClip (p, s.one, PPQ * 4, PPQ * 8); });   // beats 4 to 12
    const Id id = s.c.project.track (s.one)->clips().front().id;
    const ModifierKeys shift (ModifierKeys::leftButtonModifier | ModifierKeys::shiftModifier);
    const auto before = s.c.history.size();
    // From three quarters along, two beats right: the end follows, by two beats.
    drag (a.view, a.at (PPQ * 10, 0), a.at (PPQ * 12, 0), shift);
    CHECK_EQ (s.c.project.clip (id)->start, PPQ * 4);
    CHECK_EQ (s.c.project.clip (id)->end(), PPQ * 14);
    CHECK_EQ (s.c.history.size(), before + 1);
    // From near its start, a beat right: the start follows.
    drag (a.view, a.at (PPQ * 5, 0), a.at (PPQ * 6, 0), shift);
    CHECK_EQ (s.c.project.clip (id)->start, PPQ * 5);
    CHECK_EQ (s.c.project.clip (id)->end(), PPQ * 14);
    // A Shift-click changes nothing.
    const auto now = s.c.history.size();
    click (a.view, a.at (PPQ * 8, 0), shift);
    CHECK_EQ (s.c.history.size(), now);
    CHECK_EQ (s.c.project.clip (id)->length, PPQ * 9);
    // Without Shift, the same drag still moves it.
    drag (a.view, a.at (PPQ * 8, 0), a.at (PPQ * 9, 0));
    CHECK_EQ (s.c.project.clip (id)->start, PPQ * 6);
    CHECK_EQ (s.c.project.clip (id)->length, PPQ * 9);
}

TEST ("arrange: dragging one of several chosen clips moves them all as far, one undo step; Option copies them all (found in the UAT)")
{
    Song s;
    ArrangeView a (s.c);
    s.c.apply ("Clips", [&] (Project& p)
    {
        edit::addMidiClip (p, s.one, 0, PPQ * 4);
        edit::addMidiClip (p, s.one, PPQ * 8, PPQ * 4);
        edit::addMidiClip (p, s.two, PPQ * 4, PPQ * 4);
    });
    const auto& ones = s.c.project.track (s.one)->clips();
    const Id first = ones[0].id, second = ones[1].id, third = s.c.project.track (s.two)->clips().front().id;
    const ModifierKeys command (ModifierKeys::leftButtonModifier | ModifierKeys::commandModifier);
    click (a.view, a.at (PPQ * 2, 0));
    click (a.view, a.at (PPQ * 10, 0), command);
    click (a.view, a.at (PPQ * 6, 1), command);
    CHECK_EQ (s.c.selectedClips.size(), (size_t) 3);
    const auto before = s.c.history.size();
    drag (a.view, a.at (PPQ * 2, 0), a.at (PPQ * 4, 0));   // two beats later
    CHECK_EQ (s.c.project.clip (first)->start, PPQ * 2);
    CHECK_EQ (s.c.project.clip (second)->start, PPQ * 10);
    CHECK_EQ (s.c.project.clip (third)->start, PPQ * 6);
    CHECK_EQ (s.c.history.size(), before + 1);
    s.c.undo();
    CHECK_EQ (s.c.project.clip (first)->start, (Tick) 0);
    CHECK_EQ (s.c.project.clip (third)->start, PPQ * 4);
}
