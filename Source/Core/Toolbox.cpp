#include "Toolbox.h"
#include "Edit.h"
#include "Platform.h"

#include <algorithm>
#include <cmath>

namespace kal::tools
{

const std::vector<Info>& all()
{
    static const std::vector<Info> list {
        { Tool::Select, "Select", "Choose, move and trim, as always.", true, true,
          { { "Normal", "Click to choose, drag to move or trim." },
            { "Click Adds or Takes Away", "Each click adds what's clicked to the choice, or takes it out." },
            { "Everything at the Click", "A click chooses every clip (or note) playing at that moment." } } },
        { Tool::Range, "Range", "Drag across tracks to choose a stretch of time; Delete clears what's in it. In the piano roll, drag to choose an area of notes.", true, true, {} },
        { Tool::Split, "Split", "Click a clip or note to cut it there. " + platform::optionClick() + " cuts it into pieces as long as from its start to the click.", true, true,
          { { "Split the Clicked One", "Only what's clicked is cut." },
            { "Split Every Chosen One", "Clicking a chosen clip or note cuts every chosen one at that point." } } },
        { Tool::Glue, "Glue", "Click a clip to join it to the next one on its track, or a note to the next of its pitch.", true, true, {} },
        { Tool::Erase, "Erase", "Click or drag over clips, notes and automation points to delete them.", true, true, {} },
        { Tool::Zoom, "Zoom", "Click to zoom in, " + platform::optionClick() + " to zoom out.", true, true,
          { { "Zoom to the Dragged Area", "Drag around what you want to fill the view." },
            { "Zoom Horizontally", "Drag right to zoom in, left to zoom out." },
            { "Zoom Vertically", "Click to make rows taller; " + platform::optionClick() + " makes them shorter." } } },
        { Tool::Mute, "Mute", "Click or drag over clips or notes to mute them, or unmute them.", true, true, {} },
        { Tool::Draw, "Draw", "Drag in an empty lane for a new MIDI clip; drag over automation to draw it. In the piano roll, click or drag to draw notes, even over others.", true, true, {} },
        { Tool::Line, "Line", "Drag a straight line of automation, or a run of notes in the piano roll.", true, true,
          { { "Linear", "A straight line." },
            { "Curve", "A bend you can change afterwards." },
            { "S-Curve", "Slow at the start and the end." },
            { "Fast Start", "Moves quickly at first, then slows." },
            { "Fast End", "Moves slowly at first, then quickly." },
            { "Steps", "Holds each value for a grid step." } } },
        { Tool::Play, "Play", "Play from where you click.", true, true,
          { { "Play While Held", "Plays from the click until you let go. In the piano roll, notes play as the pointer passes over them." },
            { "Play from the Click", "Click to play from there; click again to stop." } } },
        { Tool::Hand, "Hand", "Drag to move around without zooming.", true, true, {} },
        { Tool::Drumstick, "Drumstick", "Click or drag to paint hits a grid step long; click a hit to take it away.", false, true, {} },
        { Tool::TimeWarp, "Time Warp", "Click to put a tempo change on the nearest grid line. Drag a beat to where it falls in the audio: the tempo before it changes to fit.", true, false, {} },
    };
    return list;
}

const Info& info (Tool t)
{
    for (auto& i : all()) if (i.tool == t) return i;
    return all().front();
}

bool availableIn (Tool t, Area a)
{
    const auto& i = info (t);
    return a == Area::Arrange ? i.inArrange : i.inRoll;
}

bool State::choose (Tool t, Area a, std::optional<int> mode)
{
    if (! availableIn (t, a)) return false;
    tool = t;
    const int count = (int) info (t).modes.size();
    if (mode && *mode >= 0 && *mode < count) modes[(size_t) t] = *mode;
    return true;
}

std::string State::describe() const
{
    const auto& i = info (tool);
    std::string s = i.name;
    if (! i.modes.empty() && mode() < (int) i.modes.size() && ! (tool == Tool::Select && mode() == selectmode::Normal))
        s += std::string (" (") + i.modes[(size_t) mode()].name + ")";
    return s;
}

//==============================================================================
std::vector<Tick> repeatedCuts (Tick start, Tick end, Tick at)
{
    std::vector<Tick> cuts;
    const Tick piece = at - start;
    if (piece <= 0 || at >= end) return cuts;
    for (Tick t = at; t < end && cuts.size() < 4096; t += piece) cuts.push_back (t);
    return cuts;
}

std::vector<Id> splitClipAt (Project& p, Id clip, const std::vector<Tick>& cuts)
{
    auto sorted = cuts;
    std::sort (sorted.rbegin(), sorted.rend());
    std::vector<Id> made;
    for (auto t : sorted)
        if (const Id r = edit::splitClip (p, clip, t)) made.push_back (r);
    std::reverse (made.begin(), made.end());
    return made;
}

std::set<Id> splitNoteAt (Project& p, Id clip, Id note, const std::vector<Tick>& cuts)
{
    auto sorted = cuts;
    std::sort (sorted.rbegin(), sorted.rend());
    std::set<Id> made;
    for (auto t : sorted)
        for (auto id : edit::splitNotes (p, clip, { note }, t)) made.insert (id);
    return made;
}

//==============================================================================
Id nextClip (const Project& p, Id clip)
{
    const Track* owner = nullptr;
    const auto* c = p.clip (clip, &owner);
    if (! c) return 0;
    const Clip* best = nullptr;
    for (auto& o : owner->clips())
        if (o.id != clip && (o.start > c->start || (o.start == c->start && o.id > c->id)))
            if (! best || o.start < best->start) best = &o;
    return best ? best->id : 0;
}

namespace
{
// Two pieces of one recording, the second starting where the first ends, in
// its file as on the timeline: exactly what Split makes.
bool meet (const Clip& a, const Clip& b)
{
    if (a.isMidi() || b.isMidi() || a.loopLength || b.loopLength) return false;
    if (b.start != a.end() || b.offset != a.offset + a.length) return false;
    auto x = a.audio, y = b.audio;
    x.fadeIn = y.fadeIn = x.fadeOut = y.fadeOut = 0;
    x.startFine = y.startFine = x.endFine = y.endFine = 0;
    return x == y && a.muted == b.muted;
}
} // namespace

Glued glueToNext (Project& p, Id clip)
{
    Glued g;
    Track* owner = nullptr;
    auto* c = p.clip (clip, &owner);
    if (! c) return g;
    g.next = nextClip (p, clip);
    auto* n = p.clip (g.next);
    if (! n || c->locked || n->locked) return g;
    if (c->isMidi() != n->isMidi()) { g.mixed = true; return g; }
    if (c->isMidi())
    {
        g.clip = edit::consolidateClips (p, { clip, g.next });
        return g;
    }
    if (! meet (*c, *n)) { g.needsRender = true; return g; }
    c->length += n->length;
    c->audio.fadeOut = n->audio.fadeOut;
    c->audio.endFine = n->audio.endFine;
    const Id gone = g.next;
    edit::removeClips (p, { gone });
    g.clip = clip;
    return g;
}

Id glueNote (Project& p, Id clip, Id note)
{
    auto* c = p.clip (clip);
    if (! c || ! c->isMidi() || c->locked) return 0;
    const Note* self = nullptr;
    for (auto& n : c->midi.notes) if (n.id == note) self = &n;
    if (! self) return 0;
    const Note* next = nullptr;
    for (auto& n : c->midi.notes)
        if (n.id != note && n.pitch == self->pitch && n.start > self->start)
            if (! next || n.start < next->start) next = &n;
    if (! next) return 0;
    edit::joinNotes (p, clip, { note, next->id });
    return note;
}

//==============================================================================
int clearRange (Project& p, const std::vector<Id>& tracks, Tick from, Tick to)
{
    if (to <= from) return 0;
    int touched = 0;
    for (auto tid : tracks)
    {
        auto* t = p.track (tid);
        if (! t) continue;
        std::vector<Id> ids;
        for (auto& c : t->clips())
            if (c.start < to && c.end() > from && ! c.locked) ids.push_back (c.id);
        std::set<Id> remove;
        for (auto id : ids)
        {
            ++touched;
            // Cut at the far edge first, so this id stays the left piece.
            if (auto* c = p.clip (id); c && to < c->end()) edit::splitClip (p, id, to);
            Id inside = id;
            if (auto* c = p.clip (id); c && from > c->start) inside = edit::splitClip (p, id, from);
            if (inside) remove.insert (inside);
        }
        edit::removeClips (p, remove);
    }
    return touched;
}

std::set<Id> clipsAt (const Project& p, Tick t)
{
    std::set<Id> out;
    for (auto& tr : p.tracks)
        for (auto& c : tr.clips())
            if (t >= c.start && t < c.end()) out.insert (c.id);
    return out;
}

std::set<Id> notesAt (const std::vector<Note>& notes, Tick t)
{
    std::set<Id> out;
    for (auto& n : notes) if (t >= n.start && t < n.end()) out.insert (n.id);
    return out;
}

//==============================================================================
namespace
{
// The bends Kalibre's automation points take (edit::valueAtTicks): a power curve.
constexpr double curveBend = 0.5, fastBend = 0.6;
double bent (double x, double curve) { return std::pow (x, std::pow (2.0, curve * 3.0)); }
} // namespace

double shaped (Shape s, double x)
{
    x = std::clamp (x, 0.0, 1.0);
    switch (s)
    {
        case Shape::Linear:
        case Shape::Steps:     return x;
        case Shape::Curve:     return bent (x, curveBend);
        case Shape::FastStart: return bent (x, -fastBend);
        case Shape::FastEnd:   return bent (x, fastBend);
        case Shape::SCurve:    return x < 0.5 ? 0.5 * bent (x * 2.0, fastBend) : 0.5 + 0.5 * bent (x * 2.0 - 1.0, -fastBend);
    }
    return x;
}

std::vector<AutoPoint> linePoints (Tick a, double va, Tick b, double vb, Shape s, Tick step)
{
    if (b < a) { std::swap (a, b); std::swap (va, vb); }
    va = std::clamp (va, 0.0, 1.0);
    vb = std::clamp (vb, 0.0, 1.0);
    if (b == a) return { { a, vb, 0, false } };
    std::vector<AutoPoint> out;
    switch (s)
    {
        case Shape::Linear:    out = { { a, va, 0, false }, { b, vb, 0, false } }; break;
        case Shape::Curve:     out = { { a, va, curveBend, false }, { b, vb, 0, false } }; break;
        case Shape::FastStart: out = { { a, va, -fastBend, false }, { b, vb, 0, false } }; break;
        case Shape::FastEnd:   out = { { a, va, fastBend, false }, { b, vb, 0, false } }; break;
        case Shape::SCurve:
        {
            const Tick m = a + (b - a) / 2;
            out = { { a, va, fastBend, false }, { m, (va + vb) / 2, -fastBend, false }, { b, vb, 0, false } };
            break;
        }
        case Shape::Steps:
        {
            step = std::max<Tick> (1, step);
            for (Tick t = a; t < b && out.size() < 4096; t += step)
                out.push_back ({ t, va + (vb - va) * (double) (t - a) / (double) (b - a), 0, true });
            out.push_back ({ b, vb, 0, false });
            break;
        }
    }
    return out;
}

std::vector<Note> lineNotes (Tick a, int pitchA, Tick b, int pitchB, Shape s, Tick step, int velocity)
{
    if (b < a) { std::swap (a, b); std::swap (pitchA, pitchB); }
    step = std::max<Tick> (1, step);
    std::vector<Note> out;
    for (Tick t = a; t <= b && out.size() < 4096; t += step)
    {
        const double x = b > a ? (double) (t - a) / (double) (b - a) : 1.0;
        Note n;
        n.start = t;
        n.length = step;
        n.pitch = std::clamp ((int) std::lround (pitchA + (pitchB - pitchA) * shaped (s, x)), 0, 127);
        n.velocity = velocity;
        out.push_back (n);
    }
    return out;
}

//==============================================================================
bool warpBeat (TempoMap& map, Tick beat, double toSeconds)
{
    if (beat <= 0) return false;
    auto points = map.tempos();
    // The tempo after the beat stays: a change there holds it.
    bool there = false;
    for (auto& tp : points) there = there || tp.tick == beat;
    if (! there)
    {
        const double after = map.bpmAt (beat);
        points.push_back ({ beat, after, false });
        std::sort (points.begin(), points.end(), [] (const TempoPoint& x, const TempoPoint& y) { return x.tick < y.tick; });
    }
    // The change before it, and where it falls (unchanged: only later tempos move).
    size_t prev = 0;
    for (size_t i = 0; i < points.size(); ++i) if (points[i].tick < beat) prev = i;
    TempoMap trial = map;
    trial.setTempos (points);
    const double from = trial.secondsAt (points[prev].tick);
    if (toSeconds <= from + 1e-6) return false;
    const double bpm = (double) (beat - points[prev].tick) / (double) PPQ * 60.0 / (toSeconds - from);
    if (bpm < TempoMap::minBpm || bpm > TempoMap::maxBpm) return false;
    points[prev].bpm = bpm;
    points[prev].rampToNext = false;
    map.setTempos (points);
    return true;
}

bool warpBeat (Project& p, Tick beat, double toSeconds)
{
    const TempoMap old = p.tempo;
    if (! warpBeat (p.tempo, beat, toSeconds)) return false;
    auto at = [&] (Tick t) { return p.tempo.tickAt (old.secondsAt (t)); };
    for (auto& t : p.tracks)
        for (auto& v : t.versions)
            for (auto& c : v.clips)
            {
                if (c.isMidi() || c.audio.warp != WarpMode::Off || c.loopLength > 0) continue;
                const Tick content = at (c.start - c.offset), start = at (c.start), end = at (c.end());
                c.start = start;
                c.offset = start - content;
                c.length = std::max<Tick> (1, end - start);
            }
    return true;
}

} // namespace kal::tools
