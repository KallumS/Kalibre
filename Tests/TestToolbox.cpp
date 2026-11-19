// The toolbox (decision 0092): the tools Control-right-click chooses, and the edits they make.
#include "Check.h"
#include "Edit.h"
#include "Playback.h"
#include "ProjectFile.h"
#include "Toolbox.h"

#include <cmath>

using namespace kal;
using namespace kal::tools;

namespace
{
Clip audioClip (Tick start, Tick length)
{
    Clip c;
    c.kind = Clip::Kind::Audio;
    c.start = start;
    c.length = length;
    c.audio.file = "take.wav";
    c.audio.sourceLength = 60;
    c.audio.fadeIn = 0.01;
    c.audio.fadeOut = 0.02;
    return c;
}
} // namespace

TEST ("toolbox: each area keeps its own tool, and each tool its mode")
{
    State arrange, roll;
    CHECK (arrange.tool == Tool::Select);
    CHECK (arrange.choose (Tool::Split, Area::Arrange, splitmode::EveryChosen));
    CHECK (arrange.tool == Tool::Split);
    CHECK_EQ (arrange.mode(), (int) splitmode::EveryChosen);
    // The Drumstick is the piano roll's; Time Warp the arrangement's.
    CHECK (! arrange.choose (Tool::Drumstick, Area::Arrange));
    CHECK (arrange.tool == Tool::Split);
    CHECK (! roll.choose (Tool::TimeWarp, Area::Roll));
    CHECK (roll.choose (Tool::Drumstick, Area::Roll));
    CHECK (roll.tool == Tool::Drumstick);
    // Picking a tool again without a mode gives the mode it had.
    CHECK (arrange.choose (Tool::Erase, Area::Arrange));
    CHECK (arrange.choose (Tool::Split, Area::Arrange));
    CHECK_EQ (arrange.mode(), (int) splitmode::EveryChosen);
    CHECK_EQ (arrange.describe(), std::string ("Split (Split Every Chosen One)"));
    // A mode that isn't there is ignored.
    CHECK (arrange.choose (Tool::Split, Area::Arrange, 7));
    CHECK_EQ (arrange.mode(), (int) splitmode::EveryChosen);
    CHECK_EQ ((int) all().size(), toolCount);
    for (int i = 0; i < toolCount; ++i) CHECK (all()[(size_t) i].tool == (Tool) i);
    CHECK_EQ (info (Tool::Line).modes.size(), size_t (6));
}

TEST ("toolbox: Option-click with Split cuts into pieces as long as the click is from the start")
{
    CHECK_EQ (repeatedCuts (0, PPQ * 4, PPQ), (std::vector<Tick> { PPQ, PPQ * 2, PPQ * 3 }));
    CHECK_EQ (repeatedCuts (PPQ, PPQ * 4, PPQ * 2), (std::vector<Tick> { PPQ * 2, PPQ * 3 }));
    CHECK (repeatedCuts (0, PPQ, 0).empty());
    CHECK (repeatedCuts (0, PPQ, PPQ).empty());

    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Drums");
    const Id c = edit::addMidiClip (p, t, 0, PPQ * 4);
    for (int i = 0; i < 4; ++i) edit::addNote (p, c, { 0, i * PPQ, PPQ / 2, 36 });
    const auto made = splitClipAt (p, c, repeatedCuts (0, PPQ * 4, PPQ));
    CHECK_EQ (made.size(), size_t (3));
    CHECK_EQ (p.track (t)->clips().size(), size_t (4));
    // The first piece keeps its id; each piece is a beat with its own note.
    CHECK_EQ (p.clip (c)->length, PPQ);
    for (auto& piece : p.track (t)->clips())
    {
        CHECK_EQ (piece.length, PPQ);
        CHECK_EQ (piece.midi.notes.size(), size_t (1));
    }

    // A note the same way.
    const Id c2 = edit::addMidiClip (p, t, PPQ * 8, PPQ * 4);
    const Id n = edit::addNote (p, c2, { 0, 0, PPQ * 2, 60 });
    const auto pieces = splitNoteAt (p, c2, n, repeatedCuts (0, PPQ * 2, PPQ / 2));
    CHECK_EQ (pieces.size(), size_t (3));
    CHECK_EQ (p.clip (c2)->midi.notes.size(), size_t (4));
    for (auto& x : p.clip (c2)->midi.notes) CHECK_EQ (x.length, PPQ / 2);
}

TEST ("toolbox: Glue joins MIDI clips into one, and a split recording back as it was")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Keys");
    const Id a = edit::addMidiClip (p, t, 0, PPQ * 4);
    const Id b = edit::addMidiClip (p, t, PPQ * 8, PPQ * 4);
    edit::addNote (p, a, { 0, 0, PPQ, 60 });
    edit::addNote (p, b, { 0, PPQ, PPQ, 64 });
    CHECK_EQ (nextClip (p, a), b);
    CHECK_EQ (nextClip (p, b), Id (0));
    const auto g = glueToNext (p, a);
    CHECK (g.clip != 0);
    CHECK_EQ (p.track (t)->clips().size(), size_t (1));
    const auto* one = p.clip (g.clip);
    CHECK_EQ (one->start, Tick (0));
    CHECK_EQ (one->length, PPQ * 12);
    CHECK_EQ (one->midi.notes.size(), size_t (2));
    CHECK_EQ (one->midi.notes[1].start, PPQ * 9);

    // Audio split, then glued: the clip it was, fades and all.
    const Id at = edit::addTrack (p, TrackKind::Track, "Vocal");
    const Id v = edit::addClip (p, at, audioClip (PPQ * 4, PPQ * 8));
    const Clip before = *p.clip (v);
    CHECK (edit::splitClip (p, v, PPQ * 7, 0.0001) != 0);
    const auto h = glueToNext (p, v);
    CHECK_EQ (h.clip, v);
    CHECK (! h.needsRender);
    CHECK_EQ (p.track (at)->clips().size(), size_t (1));
    CHECK (*p.clip (v) == before);

    // Two different recordings: only Consolidate can join them; nothing changes here.
    auto other = audioClip (PPQ * 12, PPQ * 4);
    other.audio.file = "other.wav";
    const Id o = edit::addClip (p, at, other);
    const Project untouched = p;
    const auto r = glueToNext (p, v);
    CHECK (r.needsRender);
    CHECK_EQ (r.next, o);
    CHECK_EQ (r.clip, Id (0));
    CHECK (p == untouched);

    // MIDI and audio can't be glued.
    const Id m = edit::addMidiClip (p, at, PPQ * 20, PPQ);
    CHECK (glueToNext (p, o).mixed);
    CHECK (p.clip (m));
}

TEST ("toolbox: Glue joins a note to the next of its pitch")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Keys");
    const Id c = edit::addMidiClip (p, t, 0, PPQ * 4);
    const Id n1 = edit::addNote (p, c, { 0, 0, PPQ / 2, 60 });
    edit::addNote (p, c, { 0, PPQ / 2, PPQ / 2, 62 });
    edit::addNote (p, c, { 0, PPQ, PPQ, 60 });
    CHECK_EQ (glueNote (p, c, n1), n1);
    CHECK_EQ (p.clip (c)->midi.notes.size(), size_t (2));
    for (auto& n : p.clip (c)->midi.notes)
        if (n.pitch == 60) { CHECK_EQ (n.start, Tick (0)); CHECK_EQ (n.length, PPQ * 2); }
    CHECK_EQ (glueNote (p, c, n1), Id (0));   // nothing after it now
}

TEST ("toolbox: Range clears a stretch of time on the tracks dragged over, nothing else")
{
    Project p;
    const Id t1 = edit::addTrack (p, TrackKind::Track, "One");
    const Id t2 = edit::addTrack (p, TrackKind::Track, "Two");
    const Id t3 = edit::addTrack (p, TrackKind::Track, "Three");
    edit::addMidiClip (p, t1, 0, PPQ * 8);
    edit::addMidiClip (p, t2, PPQ * 2, PPQ * 2);   // wholly inside
    const Id locked = edit::addMidiClip (p, t2, PPQ * 5, PPQ);
    p.clip (locked)->locked = true;
    edit::addMidiClip (p, t3, 0, PPQ * 8);
    CHECK_EQ (clearRange (p, { t1, t2 }, PPQ, PPQ * 6), 2);
    // Track one: [0, 1) and [6, 8) left.
    const auto& one = p.track (t1)->clips();
    CHECK_EQ (one.size(), size_t (2));
    CHECK_EQ (one[0].start, Tick (0));
    CHECK_EQ (one[0].length, PPQ);
    CHECK_EQ (one[1].start, PPQ * 6);
    CHECK_EQ (one[1].end(), PPQ * 8);
    // Track two: the clip inside is gone; the locked one stays.
    CHECK_EQ (p.track (t2)->clips().size(), size_t (1));
    CHECK (p.clip (locked));
    // Track three wasn't in the range.
    CHECK_EQ (p.track (t3)->clips().size(), size_t (1));
    CHECK_EQ (p.track (t3)->clips()[0].length, PPQ * 8);
}

TEST ("toolbox: Everything at the Click finds what's playing at that moment")
{
    Project p;
    const Id t1 = edit::addTrack (p, TrackKind::Track, "One");
    const Id t2 = edit::addTrack (p, TrackKind::Track, "Two");
    const Id a = edit::addMidiClip (p, t1, 0, PPQ * 4);
    const Id b = edit::addMidiClip (p, t2, PPQ * 2, PPQ * 4);
    edit::addMidiClip (p, t2, PPQ * 8, PPQ * 4);
    CHECK_EQ (clipsAt (p, PPQ * 3), (std::set<Id> { a, b }));
    CHECK_EQ (clipsAt (p, PPQ * 4), (std::set<Id> { b }));
    std::vector<Note> notes { { 1, 0, PPQ, 60 }, { 2, 0, PPQ * 2, 64 }, { 3, PPQ, PPQ, 67 } };
    CHECK_EQ (notesAt (notes, PPQ / 2), (std::set<Id> { 1, 2 }));
    CHECK_EQ (notesAt (notes, PPQ), (std::set<Id> { 2, 3 }));
}

TEST ("toolbox: the Line tool's shapes run from one end to the other")
{
    for (auto s : { Shape::Linear, Shape::Curve, Shape::SCurve, Shape::FastStart, Shape::FastEnd, Shape::Steps })
    {
        CHECK (std::abs (shaped (s, 0.0)) < 1e-9);
        CHECK (std::abs (shaped (s, 1.0) - 1.0) < 1e-9);
        double last = -1;
        for (int i = 0; i <= 20; ++i) { const double v = shaped (s, i / 20.0); CHECK (v >= last); last = v; }
    }
    CHECK (shaped (Shape::FastStart, 0.25) > 0.25);
    CHECK (shaped (Shape::FastEnd, 0.25) < 0.25);
    CHECK (std::abs (shaped (Shape::SCurve, 0.5) - 0.5) < 1e-9);
    CHECK (shaped (Shape::SCurve, 0.1) < 0.1);
    CHECK (shaped (Shape::SCurve, 0.9) > 0.9);

    // Automation drawn by the points matches the shape.
    for (auto s : { Shape::Linear, Shape::Curve, Shape::SCurve, Shape::FastStart, Shape::FastEnd })
    {
        AutomationLane lane;
        for (auto& pt : linePoints (PPQ * 4, 0.8, 0, 0.2, s, PPQ)) edit::addPoint (lane, pt);   // drawn right to left
        CHECK_EQ (lane.points.front().tick, Tick (0));
        for (int i = 0; i <= 8; ++i)
        {
            const double x = i / 8.0;
            const double want = 0.2 + 0.6 * shaped (s, x);
            CHECK (std::abs (edit::valueAt (lane, (Tick) (x * PPQ * 4), 0) - want) < 1e-3);
        }
    }
    // Steps: held a grid step each.
    const auto steps = linePoints (0, 0, PPQ * 4, 1, Shape::Steps, PPQ);
    CHECK_EQ (steps.size(), size_t (5));
    CHECK (steps[0].step && steps[3].step && ! steps[4].step);
    CHECK (std::abs (steps[2].value - 0.5) < 1e-9);

    // A run of notes in the piano roll.
    const auto notes = lineNotes (0, 60, PPQ * 3, 72, Shape::Linear, PPQ, 90);
    CHECK_EQ (notes.size(), size_t (4));
    CHECK_EQ (notes[0].pitch, 60);
    CHECK_EQ (notes[2].pitch, 68);
    CHECK_EQ (notes[3].pitch, 72);
    CHECK_EQ (notes[3].start, PPQ * 3);
    CHECK_EQ (notes[1].length, PPQ);
    CHECK_EQ (notes[1].velocity, 90);
}

TEST ("toolbox: Time Warp moves a beat to a moment by the tempo before it")
{
    TempoMap m;   // 120 BPM: a beat is half a second
    const Tick bar3 = PPQ * 8;
    CHECK (std::abs (m.secondsAt (bar3) - 4.0) < 1e-9);
    // The third bar heard at 4.4 s: the two bars before it slow to fit.
    CHECK (warpBeat (m, bar3, 4.4));
    CHECK (std::abs (m.secondsAt (bar3) - 4.4) < 1e-6);
    CHECK (std::abs (m.bpmAt (0) - 120.0 * 4.0 / 4.4) < 1e-6);
    // After it, the tempo is as it was: a bar later is still two seconds later.
    CHECK (std::abs (m.bpmAt (bar3) - 120.0) < 1e-9);
    CHECK (std::abs (m.secondsAt (bar3 + PPQ * 4) - 6.4) < 1e-6);
    // Again, from the change now at bar 3: bar 5 earlier; bar 3 stays.
    const Tick bar5 = PPQ * 16;
    CHECK (warpBeat (m, bar5, 8.0));
    CHECK (std::abs (m.secondsAt (bar3) - 4.4) < 1e-6);
    CHECK (std::abs (m.secondsAt (bar5) - 8.0) < 1e-6);
    CHECK (std::abs (m.bpmAt (bar3) - 120.0 * 4.0 / 3.6) < 1e-6);
    // A beat can't go before the change before it.
    const auto kept = m;
    CHECK (! warpBeat (m, bar5, 4.0));
    CHECK (m == kept);
}

TEST ("toolbox: a muted note is kept and saved, but never played")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Keys");
    const Id c = edit::addMidiClip (p, t, 0, PPQ * 4);
    edit::addNote (p, c, { 0, 0, PPQ, 60 });
    const Id n = edit::addNote (p, c, { 0, PPQ, PPQ, 64 });
    auto noteOns = [&] (const Project& q)
    {
        int on = 0;
        for (auto& m : playback::compile (q, *q.track (t)).midi)
            if ((m.bytes[0] & 0xF0) == 0x90 && m.bytes[2] > 0) ++on;
        return on;
    };
    CHECK_EQ (noteOns (p), 2);
    for (auto& x : p.clip (c)->midi.notes) if (x.id == n) x.muted = true;
    CHECK_EQ (noteOns (p), 1);
    const auto back = *file::load (file::save (p));
    CHECK (back.clip (c)->midi.notes[1].muted);
    CHECK (! back.clip (c)->midi.notes[0].muted);
}

TEST ("toolbox: Time Warp leaves audio where it's heard and moves the grid under it")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Drums");
    const Id a = edit::addClip (p, t, audioClip (PPQ * 4, PPQ * 8));   // bar 2 to bar 4, at 120: 2 s to 6 s
    p.clip (a)->offset = PPQ;                                         // starting a beat into its file
    const Id m = edit::addMidiClip (p, t, PPQ * 4, PPQ * 4);
    auto heard = [&] (Tick tick) { return p.tempo.secondsAt (tick); };
    const double startAt = heard (PPQ * 4), endAt = heard (PPQ * 12), contentAt = heard (PPQ * 3);
    CHECK (warpBeat (p, PPQ * 8, 3.6));
    // The audio: same moments, start, end and where its file starts.
    const auto* c = p.clip (a);
    CHECK (std::abs (heard (c->start) - startAt) < 0.002);
    CHECK (std::abs (heard (c->end()) - endAt) < 0.002);
    CHECK (std::abs (heard (c->start - c->offset) - contentAt) < 0.002);
    // The MIDI clip keeps its beats.
    CHECK_EQ (p.clip (m)->start, PPQ * 4);
    CHECK (std::abs (heard (PPQ * 8) - 3.6) < 1e-6);
}
