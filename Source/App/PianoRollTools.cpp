// The piano roll's tools (decision 0092): what a left click in the notes does
// when a tool other than Select is chosen from the toolbox (Control-right-click).
// With Select, the piano roll's own rules (decision 0009) are unchanged.
#include "PianoRoll.h"
#include "ToolPicker.h"

using namespace juce;

namespace kal
{

using tools::Tool;

void PianoRoll::showToolBox (Point<int> screen)
{
    toolDrag.reset();
    ToolBox::show (c, tools::Area::Roll, screen);
}

Tick PianoRoll::toolSnap (Tick rel, bool free) const
{
    if (free || ! c.snap) return rel;
    const Tick b = base();
    return c.snapTick (rel + b) - b;
}

Tick PianoRoll::toolSnapDown (Tick rel, bool free) const
{
    if (free || ! c.snap) return rel;
    const Tick b = base();
    return c.snapDown (rel + b) - b;
}

const Note* PianoRoll::noteUnder (Point<int> p) const
{
    const auto* clip = c.project.clip (c.selectedClip);
    if (! clip) return nullptr;
    return roll::noteAt (clip->midi.notes, relAt ((float) p.x), pitchAt ((float) p.y), timeline.ticksFor ((float) theme::px (6)));
}

//==============================================================================
bool PianoRoll::toolDown (const MouseEvent& e)
{
    auto* clip = c.clip();
    if (! clip || ! clip->isMidi() || ! noteArea().contains (e.getPosition())) return false;
    const auto& state = c.rollTool;
    const Id cid = clip->id;
    const auto* note = noteUnder (e.getPosition());
    const Tick rel = relAt ((float) e.x);
    const bool free = e.mods.isCommandDown();

    ToolDrag d;
    d.tool = state.tool;
    d.mode = state.mode();
    d.down = d.at = e.getPosition();
    d.downTick = toolSnap (rel, free);
    d.downPitch = pitchAt ((float) e.y);
    d.option = e.mods.isAltDown();

    switch (state.tool)
    {
        case Tool::Select:
        {
            if (d.mode == tools::selectmode::EverythingAtClick)
            {
                c.selectedNotes = tools::notesAt (clip->midi.notes, rel);
                c.viewChanged();
                return true;
            }
            if (d.mode == tools::selectmode::ClickAddsOrRemoves && note && ! (e.mods.isShiftDown() || e.mods.isAltDown()))
            {
                if (c.selectedNotes.count (note->id)) c.selectedNotes.erase (note->id);
                else c.selectedNotes.insert (note->id);
                noteAnchor = note->id;
                c.viewChanged();
                return true;
            }
            return false;
        }
        case Tool::Range:
            return false;   // pointerFor makes the drag Shift's: choosing an area
        case Tool::Split:
        {
            if (! note) return true;
            const Id nid = note->id;
            const Tick at = toolSnap (rel, free);
            if (d.option)
            {
                const auto cuts = tools::repeatedCuts (note->start, note->end(), at);
                std::set<Id> made;
                c.apply ("Split Into Pieces", [&] (Project& p) { made = tools::splitNoteAt (p, cid, nid, cuts); });
                c.selectedNotes = made;
                c.selectedNotes.insert (nid);
            }
            else
            {
                std::set<Id> ids { nid };
                if (d.mode == tools::splitmode::EveryChosen && c.selectedNotes.count (nid)) ids = c.selectedNotes;
                c.apply (ids.size() == 1 ? "Split Note" : "Split Notes", [&] (Project& p) { edit::splitNotes (p, cid, ids, at); });
            }
            return true;
        }
        case Tool::Glue:
        {
            if (! note) return true;
            const Id nid = note->id;
            Project trial = c.project;
            if (tools::glueNote (trial, cid, nid))
            {
                c.apply ("Glue Notes", [&] (Project& p) { tools::glueNote (p, cid, nid); });
                c.selectedNotes = { nid };
            }
            else c.say ("There's no later note of this pitch to glue it to.");
            return true;
        }
        case Tool::Erase:
        case Tool::Mute:
        {
            if (state.tool == Tool::Mute && ! note) return true;
            d.before = c.project;
            if (note) d.muteTo = ! note->muted;
            toolDrag = d;
            if (state.tool == Tool::Erase) eraseNoteAt (e.getPosition()); else muteNoteAt (e.getPosition());
            return true;
        }
        case Tool::Zoom:
        {
            if (d.mode == tools::zoommode::Vertically)
            {
                rowHeight = jlimit (6.0f, 28.0f, rowHeight + (d.option ? -2.0f : 2.0f));
                repaint();
                return true;
            }
            toolDrag = d;
            return true;
        }
        case Tool::Draw:
        {
            // A note where clicked, even over another; drag to lengthen it.
            Note n;
            n.start = toolSnapDown (rel, free);
            n.pitch = d.downPitch;
            n.length = lastLength > 0 ? lastLength : c.grid;
            n.velocity = 100;
            d.downTick = n.start;
            d.adding = { n };
            toolDrag = d;
            c.engine.audition (c.focusTrack, n.pitch, 100, 0.25);
            repaint();
            return true;
        }
        case Tool::Line:
        {
            d.downTick = toolSnapDown (rel, free);
            d.adding = tools::lineNotes (d.downTick, d.downPitch, d.downTick, d.downPitch, (tools::Shape) d.mode, c.grid, 100);
            toolDrag = d;
            repaint();
            return true;
        }
        case Tool::Play:
        {
            if (note)
            {
                d.heard = note->id;
                c.engine.audition (c.focusTrack, note->pitch, note->velocity, 0.4);
                toolDrag = d;
                return true;
            }
            if (d.mode == tools::playmode::FromClick && c.engine.isPlaying()) { c.stop(); return true; }
            if (c.engine.isPlaying()) c.stop();
            c.locate (base() + rel);
            c.play();
            toolDrag = d;
            return true;
        }
        case Tool::Hand:
            d.startTick0 = timeline.startTick;
            d.scrollPitch0 = scrollPitch;
            toolDrag = d;
            return true;
        case Tool::Drumstick:
        {
            if (note)
            {
                // A hit clicked is taken away.
                const Id nid = note->id;
                c.apply ("Remove Hit", [&] (Project& p) { edit::removeNotes (p, cid, { nid }); });
                c.selectedNotes.erase (nid);
                return true;
            }
            toolDrag = d;
            paintHitAt (e.getPosition());
            return true;
        }
        case Tool::TimeWarp:
            return false;
    }
    return false;
}

void PianoRoll::toolDragTo (const MouseEvent& e)
{
    if (! toolDrag) return;
    auto& d = *toolDrag;
    const auto prev = d.at;
    d.at = e.getPosition();
    d.moved = d.moved || e.getDistanceFromDragStart() > 3;
    const Tick rel = relAt ((float) e.x);
    const bool free = e.mods.isCommandDown();
    switch (d.tool)
    {
        case Tool::Erase:
        case Tool::Mute:
        case Tool::Drumstick:
        {
            // Every point along the way, so a quick drag misses nothing.
            const auto to = e.getPosition();
            const int steps = jmax (1, roundToInt (prev.getDistanceFrom (to) / 3.0f));
            for (int i = 1; i <= steps; ++i)
            {
                const auto p = prev + (to - prev) * i / steps;
                if (d.tool == Tool::Erase) eraseNoteAt (p);
                else if (d.tool == Tool::Mute) muteNoteAt (p);
                else paintHitAt ({ p.x, to.y });
            }
            break;
        }
        case Tool::Draw:
        {
            auto& n = d.adding.front();
            n.length = std::max (c.snap ? c.grid : Tick (1), toolSnap (rel, free) - n.start);
            repaint();
            break;
        }
        case Tool::Line:
            d.adding = tools::lineNotes (d.downTick, d.downPitch, toolSnapDown (rel, free), pitchAt ((float) e.y), (tools::Shape) d.mode, c.grid, 100);
            repaint();
            break;
        case Tool::Play:
            if (d.heard)
                if (const auto* n = noteUnder (e.getPosition()); n && n->id != d.heard)
                {
                    d.heard = n->id;
                    c.engine.audition (c.focusTrack, n->pitch, n->velocity, 0.3);
                }
            break;
        case Tool::Zoom:
            if (d.mode == tools::zoommode::Horizontally)
                timeline.zoomAround ((float) (d.down.x - keyboardWidth()), std::exp ((double) (e.x - prev.x) * 0.01));
            repaint();
            break;
        case Tool::Hand:
            timeline.startTick = std::max (0.0, d.startTick0 - (double) (e.x - d.down.x) / timeline.pixelsPerTick);
            scrollPitch = jlimit ((float) noteArea().getHeight() / rowHeight, 127.0f, d.scrollPitch0 + (float) (e.y - d.down.y) / rowHeight);
            repaint();
            break;
        case Tool::Select:
        case Tool::Range:
        case Tool::Split:
        case Tool::Glue:
        case Tool::TimeWarp:
            break;
    }
}

void PianoRoll::toolUp (const MouseEvent& e)
{
    if (! toolDrag) return;
    auto d = *toolDrag;
    toolDrag.reset();
    const Id cid = c.selectedClip;
    switch (d.tool)
    {
        case Tool::Erase:
            c.applyLive (d.before, d.changed == 1 ? "Erase Note" : "Erase Notes");
            break;
        case Tool::Mute:
            c.applyLive (d.before, d.muteTo ? "Mute Notes" : "Unmute Notes");
            break;
        case Tool::Draw:
        {
            roll::Outcome o;
            o.action = roll::Action::AddNote;
            o.added = d.adding.front();
            lastLength = o.added.length;
            commit (o);   // as a click in empty space adds one: into a loop, lengthening the clip
            break;
        }
        case Tool::Line:
        case Tool::Drumstick:
        {
            if (d.adding.empty()) break;
            std::set<Id> made;
            const auto notes = d.adding;
            c.apply (d.tool == Tool::Line ? "Draw a Line of Notes" : "Paint Hits", [&] (Project& p)
            {
                for (auto n : notes) made.insert (edit::addNote (p, cid, n));
            });
            c.selectedNotes = made;
            break;
        }
        case Tool::Zoom:
        {
            if (d.mode == tools::zoommode::DraggedArea && d.moved && std::abs (e.x - d.down.x) > 4)
            {
                const auto area = noteArea();
                const Tick a = relAt ((float) std::min (d.down.x, e.x)), b = relAt ((float) std::max (d.down.x, e.x));
                timeline.pixelsPerTick = jlimit (0.0005, 2.0, (double) area.getWidth() / (double) jmax<Tick> (1, b - a));
                timeline.startTick = std::max (0.0, (double) (a + base()));
                const int hi = pitchAt ((float) std::min (d.down.y, e.y)), lo = pitchAt ((float) std::max (d.down.y, e.y));
                rowHeight = jlimit (6.0f, 28.0f, (float) area.getHeight() / (float) jmax (1, hi - lo + 1));
                scrollPitch = jlimit ((float) area.getHeight() / rowHeight, 127.0f, (float) hi + 1.0f);
            }
            else if (! d.moved) timeline.zoomAround ((float) (e.x - keyboardWidth()), d.option ? 0.5 : 2.0);
            break;
        }
        case Tool::Play:
            if (! d.heard && d.mode == tools::playmode::WhileHeld) c.stop();
            break;
        case Tool::Hand:
        case Tool::Select:
        case Tool::Range:
        case Tool::Split:
        case Tool::Glue:
        case Tool::TimeWarp:
            break;
    }
    repaint();
}

//==============================================================================
void PianoRoll::eraseNoteAt (Point<int> p)
{
    auto& d = *toolDrag;
    const auto* note = noteUnder (p);
    if (! note) return;
    const Id nid = note->id;
    edit::removeNotes (c.project, c.selectedClip, { nid });
    c.selectedNotes.erase (nid);
    ++d.changed;
    repaint();
}

void PianoRoll::muteNoteAt (Point<int> p)
{
    auto& d = *toolDrag;
    const auto* note = noteUnder (p);
    if (! note || note->muted == d.muteTo) return;
    std::set<Id> ids { note->id };
    if (c.selectedNotes.count (note->id)) ids = c.selectedNotes;   // a chosen note mutes every chosen one with it
    if (auto* clip = c.clip())
        for (auto& n : clip->midi.notes) if (ids.count (n.id)) n.muted = d.muteTo;
    ++d.changed;
    repaint();
}

void PianoRoll::paintHitAt (Point<int> p)
{
    auto& d = *toolDrag;
    const auto* clip = c.clip();
    if (! clip || ! noteArea().contains (p)) return;
    const Tick at = toolSnapDown (relAt ((float) p.x), false);
    const int pitch = pitchAt ((float) p.y);
    // One hit a grid step a row: none where one starts already.
    for (auto& n : clip->midi.notes) if (n.pitch == pitch && n.start == at) return;
    for (auto& n : d.adding) if (n.pitch == pitch && n.start == at) return;
    Note n;
    n.start = at;
    n.pitch = pitch;
    n.length = c.grid;
    n.velocity = 100;
    d.adding.push_back (n);
    c.engine.audition (c.focusTrack, pitch, 100, 0.15);
    repaint();
}

void PianoRoll::paintTool (Graphics& g)
{
    if (! toolDrag) return;
    const auto& d = *toolDrag;
    const auto area = noteArea();
    g.saveState();
    g.reduceClipRegion (area);
    g.setColour (theme::accent);
    for (auto& n : d.adding)
        g.fillRoundedRectangle (Rectangle<float> (xOf (n.start), yOf (n.pitch) + 1.0f, jmax (3.0f, xOf (n.end()) - xOf (n.start) - 1.0f), rowHeight - 2.0f), 2.0f);
    if (d.tool == Tool::Zoom && d.mode == tools::zoommode::DraggedArea && d.moved)
    {
        const auto box = Rectangle<int> (d.down, d.at).toFloat();
        g.setColour (theme::playhead.withAlpha (0.12f));
        g.fillRect (box);
        g.setColour (theme::playhead);
        g.drawRect (box, 1.0f);
    }
    g.restoreState();
}

} // namespace kal
