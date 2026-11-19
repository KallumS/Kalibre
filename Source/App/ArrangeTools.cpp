// The arrangement's tools (decision 0092): what a left click in the lanes does
// when a tool other than Select is chosen from the toolbox (Control-right-click).
#include "Arrange.h"
#include "ToolBox.h"

using namespace juce;

namespace kal
{

using tools::Tool;

void Arrange::showToolBox (Point<int> screen)
{
    toolDrag.reset();
    drag = Drag::None;
    ToolBox::show (c, tools::Area::Arrange, screen);
}

Rectangle<int> Arrange::rowBounds (const Row& r) const
{
    const auto lanes = laneArea();
    return { lanes.getX(), lanes.getY() + r.y - scrollY, lanes.getWidth(), r.h };
}

double Arrange::automationValueAt (const Row& r, int y) const
{
    const auto rr = rowBounds (r);
    return jlimit (0.0, 1.0, (double) ((float) rr.getBottom() - 3 - (float) y) / (rr.getHeight() - 6));
}

//==============================================================================
bool Arrange::toolDown (const MouseEvent& e)
{
    const auto& state = c.arrangeTool;
    const Tool tool = state.tool;
    if (tool != Tool::Range) c.range = {};
    const auto lanes = laneArea();
    const bool inLanes = lanes.contains (e.getPosition());
    const bool inRuler = e.y < lanes.getY() && e.x >= lanes.getX();
    if (! inLanes && ! (inRuler && tool == Tool::TimeWarp)) return false;

    const auto* row = inLanes ? rowAt (e.y) : nullptr;
    const Track* owner = nullptr;
    const auto* clip = inLanes ? clipAt (e.getPosition(), &owner) : nullptr;
    const Tick at = tickAt ((float) e.x);
    const Tick snapped = e.mods.isCommandDown() ? at : c.arrangeSnap (at);

    ToolDrag d;
    d.tool = tool;
    d.mode = state.mode();
    d.down = d.at = e.getPosition();
    d.downTick = d.lastTick = snapped;
    d.track = row ? row->track : 0;
    d.automation = row ? row->automation : -1;
    d.option = e.mods.isAltDown();
    d.command = e.mods.isCommandDown();

    switch (tool)
    {
        case Tool::Select:
        {
            if (d.mode != tools::selectmode::EverythingAtClick || ! row || ! row->isClips()) return false;
            c.selectedClips = tools::clipsAt (c.project, at);
            c.selectedClip = clip ? clip->id : 0;
            c.say (String ((int) c.selectedClips.size()) + (c.selectedClips.size() == 1 ? " clip" : " clips") + " chosen, everything playing at "
                   + String (c.project.tempo.formatBarBeat (at)) + ".");
            c.viewChanged();
            return true;
        }
        case Tool::Range:
        {
            if (! row) return false;
            c.selectedClips.clear();
            c.range = { { row->track }, snapped, snapped };
            toolDrag = d;
            repaint();
            return true;
        }
        case Tool::Split:
        {
            if (! clip) return true;
            const Id id = clip->id;
            if (clip->locked || owner->locked) { c.say ("That clip is locked.", true); return true; }
            std::vector<Id> targets { id };
            if (d.mode == tools::splitmode::EveryChosen && c.selectedClips.count (id))
                targets.assign (c.selectedClips.begin(), c.selectedClips.end());
            const Tick piece = snapped - clip->start;
            if (d.option)
            {
                // Pieces as long as from the clicked one's start to the click, in each clip from its own start.
                c.apply ("Split Into Pieces", [&] (Project& p)
                {
                    for (auto t : targets)
                        if (auto* cl = p.clip (t)) tools::splitClipAt (p, t, tools::repeatedCuts (cl->start, cl->end(), cl->start + piece));
                });
            }
            else if (targets.size() == 1)
            {
                const auto edge = c.zeroEdge (*clip, snapped);   // onto a zero crossing, when that's on (decision 0054)
                c.apply ("Split Clip", [&] (Project& p) { edit::splitClip (p, id, edge.tick, edge.fine); });
            }
            else
                c.apply ("Split Clips", [&] (Project& p) { for (auto t : targets) edit::splitClip (p, t, snapped); });
            return true;
        }
        case Tool::Glue:
        {
            if (! clip) return true;
            const Id id = clip->id;
            Project trial = c.project;
            const auto g = tools::glueToNext (trial, id);
            if (g.clip)
            {
                Id made = 0;
                c.apply ("Glue", [&] (Project& p) { made = tools::glueToNext (p, id).clip; });
                c.selectClip (made, false);
            }
            else if (g.needsRender)
            {
                c.consolidate ({ id, g.next });
                c.say ("They were two different recordings, so they were rendered into one new file (Consolidate).", false, true);
            }
            else if (g.mixed) c.say ("A MIDI clip and an audio clip can't be glued.", true);
            else if (! g.next) c.say ("There's no clip after this one on its track to glue it to.");
            else c.say ("A locked clip can't be glued.", true);
            return true;
        }
        case Tool::Erase:
        case Tool::Mute:
        {
            if (! row || row->take >= 0) return true;
            if (tool == Tool::Mute && ! clip) return true;
            d.before = c.project;
            if (tool == Tool::Mute) d.muteTo = ! clip->muted;
            toolDrag = d;
            if (tool == Tool::Erase) eraseAt (e.getPosition()); else muteAt (e.getPosition());
            return true;
        }
        case Tool::Zoom:
        {
            if (d.mode == tools::zoommode::Vertically)
            {
                const bool out = d.option;
                c.apply (out ? "Shorter Tracks" : "Taller Tracks", [out] (Project& p)
                {
                    for (auto& t : p.tracks) t.height = jlimit (t.isFolder() ? 28 : 36, 240, roundToInt ((float) t.height * (out ? 0.8f : 1.25f)));
                });
                return true;
            }
            toolDrag = d;
            return true;
        }
        case Tool::Draw:
        {
            if (! row || row->take >= 0) return true;
            if (row->automation >= 0)
            {
                d.before = c.project;
                toolDrag = d;
                drawAutomationTo (at, automationValueAt (*row, e.y));
                return true;
            }
            if (clip) return false;   // on a clip, Draw chooses and moves it as Select does
            const auto* t = c.project.track (row->track);
            if (! t || ! t->holdsClips() || t->locked) return true;
            toolDrag = d;
            repaint();
            return true;
        }
        case Tool::Line:
        {
            if (! row || row->automation < 0) return true;
            auto* t = c.project.track (row->track);
            d.before = c.project;
            d.pointsBefore = t->automation[(size_t) row->automation].points;
            toolDrag = d;
            toolDragTo (e);
            return true;
        }
        case Tool::Play:
        {
            if (d.mode == tools::playmode::FromClick && c.engine.isPlaying()) { c.stop(); return true; }
            if (c.engine.isPlaying()) c.stop();
            c.locate (at);
            c.play();
            toolDrag = d;
            return true;
        }
        case Tool::Hand:
        {
            d.startTick0 = timeline.startTick;
            d.scrollY0 = scrollY;
            toolDrag = d;
            return true;
        }
        case Tool::TimeWarp:
        {
            // The grid line nearest the press: a beat to move.
            d.downTick = c.arrangeSnap (at);
            if (d.downTick <= 0) return true;
            toolDrag = d;
            repaint();
            return true;
        }
        case Tool::Drumstick:
            return false;
    }
    return false;
}

void Arrange::toolDragTo (const MouseEvent& e)
{
    if (! toolDrag) return;
    auto& d = *toolDrag;
    const auto prev = d.at;
    d.at = e.getPosition();
    d.moved = d.moved || e.getDistanceFromDragStart() > 3;
    const Tick at = tickAt ((float) e.x);
    const Tick snapped = e.mods.isCommandDown() ? at : c.arrangeSnap (std::max<Tick> (0, at));
    switch (d.tool)
    {
        case Tool::Range:
        {
            // Every track from the one pressed on to the one under the pointer, in the arrangement's order.
            const auto order = c.arrangeOrder();
            const auto* row = rowAt (jlimit (laneArea().getY(), laneArea().getBottom() - 1, e.y));
            const Id to = row ? row->track : d.track;
            const auto a = std::find (order.begin(), order.end(), d.track), b = std::find (order.begin(), order.end(), to);
            std::vector<Id> tracks;
            if (a != order.end() && b != order.end())
                for (auto it = std::min (a, b); it <= std::max (a, b); ++it)
                    if (auto* t = c.project.track (*it); t && t->holdsClips()) tracks.push_back (*it);
            c.range = { tracks, std::min (d.downTick, snapped), std::max (d.downTick, snapped) };
            c.viewChanged();
            break;
        }
        case Tool::Erase:
        case Tool::Mute:
        {
            // Every point along the way, so a quick drag misses nothing.
            const auto to = e.getPosition();
            const int steps = jmax (1, roundToInt (prev.getDistanceFrom (to) / 3.0f));
            for (int i = 1; i <= steps; ++i)
            {
                const auto p = prev + (to - prev) * i / steps;
                if (d.tool == Tool::Erase) eraseAt (p); else muteAt (p);
            }
            break;
        }
        case Tool::Zoom:
            if (d.mode == tools::zoommode::Horizontally)
            {
                // Right to zoom in, left to zoom out, around where it was pressed.
                timeline.zoomAround ((float) (d.down.x - headerWidth()), std::exp ((double) (e.x - prev.x) * 0.01));
                layoutRows();
            }
            repaint();
            break;
        case Tool::Draw:
            if (d.automation >= 0)
            {
                if (const auto* row = [&]() -> const Row* { for (auto& r : rows) if (r.track == d.track && r.automation == d.automation) return &r; return nullptr; }())
                    drawAutomationTo (std::max<Tick> (0, at), automationValueAt (*row, e.y));
            }
            else repaint();
            break;
        case Tool::Line:
        {
            auto* t = c.project.track (d.track);
            const Row* row = nullptr;
            for (auto& r : rows) if (r.track == d.track && r.automation == d.automation) row = &r;
            if (! t || ! row || d.automation >= (int) t->automation.size()) break;
            auto& lane = t->automation[(size_t) d.automation];
            lane.points = d.pointsBefore;
            const double v0 = automationValueAt (*row, d.down.y), v1 = automationValueAt (*row, e.y);
            const Tick a = std::min (d.downTick, snapped), b = std::max (d.downTick, snapped);
            edit::removePointsIn (lane, a, b);
            for (auto& pt : tools::linePoints (d.downTick, v0, snapped, v1, (tools::Shape) d.mode, c.arrangeGrid)) edit::addPoint (lane, pt);
            c.viewChanged();
            break;
        }
        case Tool::Hand:
        {
            timeline.startTick = std::max (0.0, d.startTick0 - (double) (e.x - d.down.x) / timeline.pixelsPerTick);
            scrollY = jlimit (0, jmax (0, contentHeight - laneArea().getHeight() + theme::px (80)), d.scrollY0 - (e.y - d.down.y));
            layoutRows();
            repaint();
            break;
        }
        case Tool::TimeWarp:
        case Tool::Play:
            repaint();
            break;
        case Tool::Select:
        case Tool::Split:
        case Tool::Glue:
        case Tool::Drumstick:
            break;
    }
}

void Arrange::toolUp (const MouseEvent& e)
{
    if (! toolDrag) return;
    auto d = *toolDrag;
    toolDrag.reset();
    const Tick at = tickAt ((float) e.x);
    const Tick snapped = e.mods.isCommandDown() ? at : c.arrangeSnap (std::max<Tick> (0, at));
    switch (d.tool)
    {
        case Tool::Range:
            if (c.range.empty()) c.range = {};
            else c.say ("A range of " + String (c.project.tempo.describeLength (c.range.from, c.range.to - c.range.from)) + " on "
                        + String ((int) c.range.tracks.size()) + (c.range.tracks.size() == 1 ? " track" : " tracks") + ". Delete clears what's in it.");
            c.viewChanged();
            break;
        case Tool::Erase:
            c.applyLive (d.before, d.changed == 1 ? "Erase" : "Erase Several");
            break;
        case Tool::Mute:
            c.applyLive (d.before, d.muteTo ? "Mute Clips" : "Unmute Clips");
            break;
        case Tool::Draw:
            if (d.automation >= 0) { c.applyLive (d.before, "Draw Automation"); break; }
            {
                const Tick a = std::min (d.downTick, snapped), b = std::max (d.downTick, snapped);
                const Tick length = b - a >= c.arrangeGrid ? b - a : c.project.tempo.meterAt (a).barTicks();
                c.newMidiClip (d.track, a, length);
            }
            break;
        case Tool::Line:
            c.applyLive (d.before, "Draw a Line");
            break;
        case Tool::Zoom:
        {
            const auto lanes = laneArea();
            if (d.mode == tools::zoommode::DraggedArea && d.moved && std::abs (e.x - d.down.x) > 4)
            {
                const Tick a = tickAt ((float) std::min (d.down.x, e.x)), b = tickAt ((float) std::max (d.down.x, e.x));
                timeline.pixelsPerTick = jlimit (0.0005, 2.0, (double) lanes.getWidth() / (double) jmax<Tick> (1, b - a));
                timeline.startTick = (double) a;
            }
            else if (! d.moved) zoomClick (e.x, d.option);
            layoutRows();
            repaint();
            break;
        }
        case Tool::Play:
            if (d.mode == tools::playmode::WhileHeld) c.stop();
            break;
        case Tool::TimeWarp:
        {
            const Tick beat = d.downTick;
            const auto& tempo = c.project.tempo;
            // Where the pointer is let go, in time as the song plays now: the beat goes there.
            const double to = d.moved ? tempo.secondsAt (std::max<Tick> (0, at)) : tempo.secondsAt (beat);
            Project trial = c.project;
            if (! tools::warpBeat (trial, beat, to))
            {
                c.say ("The beat can't go there: it would come before the tempo change before it.", true);
                break;
            }
            c.apply (d.moved ? "Time Warp" : "Add Tempo Change", [beat, to] (Project& p) { tools::warpBeat (p, beat, to); });
            if (d.moved)
                c.say ("Bar " + String (c.project.tempo.formatBarBeat (beat)) + " now falls where you let go; the tempo before it is "
                       + String (c.project.tempo.bpmAt (beat - 1), 2) + " BPM.", false, true);
            break;
        }
        case Tool::Hand:
        case Tool::Select:
        case Tool::Split:
        case Tool::Glue:
        case Tool::Drumstick:
            break;
    }
    repaint();
}

void Arrange::zoomClick (int x, bool out)
{
    timeline.zoomAround ((float) (x - headerWidth()), out ? 0.5 : 2.0);
}

//==============================================================================
void Arrange::eraseAt (Point<int> p)
{
    auto& d = *toolDrag;
    const auto* row = rowAt (p.y);
    if (! row || row->take >= 0) return;
    if (row->isClips())
    {
        const Track* owner = nullptr;
        if (const auto* clip = clipAt (p, &owner); clip && ! clip->locked && ! owner->locked)
        {
            edit::removeClips (c.project, { clip->id });
            c.selectedClips.erase (clip->id);
            ++d.changed;
            c.viewChanged();
        }
        return;
    }
    if (row->automation < 0) return;
    auto* t = c.project.track (row->track);
    auto& lane = t->automation[(size_t) row->automation];
    const auto rr = rowBounds (*row);
    const float reach = (float) theme::px (6);
    const auto before = lane.points.size();
    // The first point holds the lane's value before anything: it stays.
    for (size_t i = lane.points.size(); i-- > 1;)
    {
        const float px = xOf (lane.points[i].tick);
        const float py = (float) rr.getBottom() - 3 - (float) lane.points[i].value * (float) (rr.getHeight() - 6);
        if (std::abs (px - (float) p.x) <= reach && std::abs (py - (float) p.y) <= reach * 2)
            lane.points.erase (lane.points.begin() + (long) i);
    }
    if (lane.points.size() != before) { d.changed += (int) (before - lane.points.size()); c.viewChanged(); }
}

void Arrange::muteAt (Point<int> p)
{
    auto& d = *toolDrag;
    const Track* owner = nullptr;
    const auto* clip = clipAt (p, &owner);
    if (! clip || clip->muted == d.muteTo) return;
    // A chosen clip mutes every chosen one with it.
    std::set<Id> ids { clip->id };
    if (c.selectedClips.count (clip->id)) ids = c.selectedClips;
    for (auto id : ids) if (auto* cl = c.project.clip (id)) cl->muted = d.muteTo;
    ++d.changed;
    c.viewChanged();
}

void Arrange::drawAutomationTo (Tick at, double v)
{
    auto& d = *toolDrag;
    auto* t = c.project.track (d.track);
    if (! t || d.automation < 0 || d.automation >= (int) t->automation.size()) return;
    auto& lane = t->automation[(size_t) d.automation];
    // Freehand: what's between the last point drawn and this one is replaced.
    if (at == d.lastTick || ! d.moved) edit::addPoint (lane, { at, v, 0, false });
    else edit::writeValue (lane, d.lastTick, at, v);
    if (at < d.lastTick)
    {
        // Drawn leftwards: the point it came from is kept, the ones passed over go.
        edit::removePointsIn (lane, at + 1, d.lastTick - 1);
        edit::addPoint (lane, { at, v, 0, false });
    }
    d.lastTick = at;
    d.moved = true;
    c.viewChanged();
}

void Arrange::toolMove (const MouseEvent& e)
{
    const Tool tool = c.arrangeTool.tool;
    const auto lanes = laneArea();
    toolHoverClip = 0;
    if (tool == Tool::Split && lanes.contains (e.getPosition()))
        if (const auto* clip = clipAt (e.getPosition()))
        {
            toolHoverClip = clip->id;
            toolHoverTick = e.mods.isCommandDown() ? tickAt ((float) e.x) : c.arrangeSnap (tickAt ((float) e.x));
        }
    repaint();
}

//==============================================================================
void Arrange::paintTool (Graphics& g)
{
    const auto lanes = laneArea();
    g.saveState();
    g.reduceClipRegion (lanes.withTop (0));

    // The range: a wash over its tracks' lanes, edged.
    if (! c.range.empty())
    {
        const float x0 = xOf (c.range.from), x1 = xOf (c.range.to);
        for (auto& r : rows)
            if (r.isClips() && std::find (c.range.tracks.begin(), c.range.tracks.end(), r.track) != c.range.tracks.end())
            {
                const auto rr = rowBounds (r).toFloat();
                const Rectangle<float> box (x0, rr.getY(), x1 - x0, rr.getHeight());
                g.setColour (theme::playhead.withAlpha (0.18f));
                g.fillRect (box);
                g.setColour (theme::playhead);
                g.drawRect (box, 1.0f);
            }
    }

    // Split: where a click cuts.
    if (c.arrangeTool.tool == Tool::Split && toolHoverClip && ! toolDrag)
        for (auto& r : rows)
            if (r.isClips())
                if (const auto* cl = c.project.clip (toolHoverClip); cl && toolHoverTick > cl->start && toolHoverTick < cl->end())
                {
                    const Track* owner = nullptr;
                    c.project.clip (toolHoverClip, &owner);
                    if (owner && owner->id == r.track)
                    {
                        const auto rr = rowBounds (r);
                        g.setColour (theme::playhead);
                        g.fillRect (xOf (toolHoverTick) - 0.5f, (float) rr.getY() + 2, 1.5f, (float) rr.getHeight() - 4);
                    }
                }

    if (toolDrag)
    {
        const auto& d = *toolDrag;
        const Tick at = tickAt ((float) d.at.x);
        if (d.tool == Tool::Zoom && d.mode == tools::zoommode::DraggedArea && d.moved)
        {
            const auto box = Rectangle<int> (d.down, d.at).withTop (lanes.getY()).withBottom (lanes.getBottom()).toFloat();
            g.setColour (theme::playhead.withAlpha (0.12f));
            g.fillRect (box);
            g.setColour (theme::playhead);
            g.drawRect (box, 1.0f);
        }
        if (d.tool == Tool::Draw && d.automation < 0)
            for (auto& r : rows)
                if (r.isClips() && r.track == d.track)
                {
                    const Tick snapped = c.arrangeSnap (std::max<Tick> (0, at));
                    const Tick a = std::min (d.downTick, snapped), b = std::max (d.downTick, snapped);
                    const Tick len = b - a >= c.arrangeGrid ? b - a : c.project.tempo.meterAt (a).barTicks();
                    const auto rr = rowBounds (r).toFloat().reduced (0, 2);
                    const Rectangle<float> box (xOf (a), rr.getY(), xOf (a + len) - xOf (a), rr.getHeight());
                    g.setColour (theme::control.withAlpha (0.35f));
                    g.fillRoundedRectangle (box, 3.0f);
                    g.setColour (theme::playhead);
                    g.drawRoundedRectangle (box, 3.0f, 1.0f);
                }
        if (d.tool == Tool::TimeWarp)
        {
            // The beat being moved, and where it's going.
            const float from = xOf (d.downTick), to = (float) d.at.x;
            g.setColour (theme::edge);
            g.fillRect (from - 1.0f, 0.0f, 2.0f, (float) getHeight());
            if (d.moved)
            {
                g.setColour (theme::playhead);
                g.fillRect (to - 1.0f, 0.0f, 2.0f, (float) getHeight());
                Path arrow;
                const float y = (float) lanes.getY() + theme::px (10);
                arrow.addArrow (Line<float> (from, y, to, y), 1.5f, 8.0f, 8.0f);
                g.fillPath (arrow);
                g.setFont (theme::font (theme::smallPoints));
                g.drawText (String ("Bar ") + String (c.project.tempo.formatBarBeat (d.downTick)) + ui::u8 (" \xe2\x86\x92 here"),
                            Rectangle<float> (to + 4, y + 4, 160, 16).toNearestInt(), Justification::centredLeft, false);
            }
        }
    }
    g.restoreState();
}

} // namespace kal
