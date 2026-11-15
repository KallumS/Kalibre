#include "Roll.h"

#include <algorithm>

namespace kal::roll
{

Zone zoneOf (const Note& n, Tick tick, int pitch, Tick edgeTicks)
{
    if (pitch != n.pitch || tick < n.start || tick >= n.end()) return Zone::Empty;
    const Tick edge = std::min (edgeTicks, n.length / 3);
    if (tick >= n.end() - edge) return Zone::EndEdge;
    if (tick < n.start + edge) return Zone::StartEdge;
    return Zone::Body;
}

const Note* noteAt (const std::vector<Note>& notes, Tick tick, int pitch, Tick edgeTicks, Zone* zone)
{
    for (auto it = notes.rbegin(); it != notes.rend(); ++it)
    {
        const auto z = zoneOf (*it, tick, pitch, edgeTicks);
        if (z != Zone::Empty)
        {
            if (zone) *zone = z;
            return &*it;
        }
    }
    if (zone) *zone = Zone::Empty;
    return nullptr;
}

std::set<Id> notesIn (const std::vector<Note>& notes, Tick from, Tick to, int lo, int hi)
{
    if (to < from) std::swap (from, to);
    if (hi < lo) std::swap (lo, hi);
    std::set<Id> out;
    for (auto& n : notes)
        if (n.pitch >= lo && n.pitch <= hi && n.end() > from && n.start < to) out.insert (n.id);
    return out;
}

//==============================================================================
Gesture::Gesture (const std::vector<Note>& n, std::set<Id> sel, Settings s)
    : notes (n), chosen (std::move (sel)), settings (std::move (s))
{
}

Tick Gesture::snapped (Tick t, bool free) const
{
    return (free || ! settings.snap) ? t : settings.snap (t);
}

Outcome Gesture::areaOutcome (const Pointer& p, Action a) const
{
    Outcome o;
    o.action = a;
    o.showArea = true;
    o.areaFrom = std::min (down.tick, p.tick);
    o.areaTo = std::max (down.tick, p.tick);
    o.areaLo = std::min (down.pitch, p.pitch);
    o.areaHi = std::max (down.pitch, p.pitch);
    o.notes = notesIn (notes, o.areaFrom, o.areaTo + 1, o.areaLo, o.areaHi);
    o.selection = chosen;
    return o;
}

Outcome Gesture::press (const Pointer& p)
{
    down = p;
    dragged = false;
    copying = false;
    last = {};
    Zone zone = Zone::Empty;
    const Note* hit = noteAt (notes, p.tick, p.pitch, settings.edgeTicks, &zone);

    if (p.right)
    {
        current = Mode::RightPending;
        return {};
    }

    if (p.shift)
    {
        if (hit)
        {
            // Shift-click on a note toggles it; Shift-drag from it still draws an area.
            current = Mode::Selecting;
            return {};
        }
        current = Mode::Selecting;
        return areaOutcome (p, Action::Select);
    }

    if (! hit)
    {
        current = Mode::Adding;
        Note n;
        n.start = std::max<Tick> (0, (settings.snapDown && ! p.command) ? settings.snapDown (p.tick) : p.tick);
        n.length = std::max (settings.defaultLength, Tick (1));
        n.pitch = std::clamp (p.pitch, 0, 127);
        n.velocity = settings.defaultVelocity;
        grabbed = n;
        Outcome o;
        o.action = Action::AddNote;
        o.added = n;
        o.audition = true;
        o.auditionPitch = n.pitch;
        o.selection = {};
        o.selectionChanged = ! chosen.empty();
        chosen.clear();
        last = o;
        return o;
    }

    grabbed = *hit;
    if (! chosen.count (hit->id))
    {
        chosen = { hit->id };
        last.selectionChanged = true;
    }
    current = zone == Zone::StartEdge ? Mode::ResizingStart
            : zone == Zone::EndEdge   ? Mode::ResizingEnd
                                      : Mode::Moving;
    copying = p.option && current == Mode::Moving;
    Outcome o;
    o.action = Action::Select;
    o.selection = chosen;
    o.selectionChanged = last.selectionChanged;
    o.audition = true;
    o.auditionPitch = hit->pitch;
    last = o;
    return o;
}

Outcome Gesture::drag (const Pointer& p)
{
    dragged = true;
    Outcome o;
    o.selection = chosen;
    switch (current)
    {
        case Mode::Adding:
        {
            // Dragging after a click draws the note out to the pointer.
            const bool free = p.command || ! settings.snap;
            const Tick end = free ? p.tick : settings.snap (p.tick);
            o.action = Action::AddNote;
            o.added = grabbed;
            o.added.length = std::max (end - grabbed.start, free ? Tick (1) : settings.minLength);
            break;
        }
        case Mode::Moving:
        {
            const Tick raw = grabbed.start + (p.tick - down.tick);
            const Tick target = std::max<Tick> (0, snapped (raw, p.command));
            o.action = copying || p.option ? Action::CopyNotes : Action::MoveNotes;
            o.notes = chosen;
            o.dt = target - grabbed.start;
            o.dPitch = p.pitch - down.pitch;
            break;
        }
        case Mode::ResizingEnd:
        {
            const Tick raw = grabbed.end() + (p.tick - down.tick);
            const Tick minLen = p.command ? 1 : settings.minLength;
            const Tick end = std::max (grabbed.start + minLen, snapped (raw, p.command));
            o.action = Action::ResizeNotes;
            o.notes = chosen;
            o.dEnd = end - grabbed.end();
            break;
        }
        case Mode::ResizingStart:
        {
            const Tick raw = grabbed.start + (p.tick - down.tick);
            const Tick minLen = p.command ? 1 : settings.minLength;
            const Tick start = std::clamp (snapped (raw, p.command), Tick (0), grabbed.end() - minLen);
            o.action = Action::ResizeNotes;
            o.notes = chosen;
            o.dStart = start - grabbed.start;
            break;
        }
        case Mode::Selecting:    o = areaOutcome (p, Action::Select); break;
        case Mode::RightPending:
        case Mode::Erasing:      current = Mode::Erasing; o = areaOutcome (p, Action::RemoveNotes); break;
        case Mode::Idle:         break;
    }
    last = o;
    return o;
}

Outcome Gesture::release (const Pointer& p)
{
    Outcome o;
    o.selection = chosen;
    switch (current)
    {
        case Mode::RightPending:
        {
            // A right click without a drag removes the note under it.
            if (const Note* hit = noteAt (notes, p.tick, p.pitch, settings.edgeTicks))
            {
                o.action = Action::RemoveNotes;
                o.notes = { hit->id };
                chosen.erase (hit->id);
                o.selection = chosen;
            }
            break;
        }
        case Mode::Erasing:
            o = areaOutcome (p, Action::RemoveNotes);
            for (auto id : o.notes) chosen.erase (id);
            o.selection = chosen;
            o.showArea = false;
            break;
        case Mode::Selecting:
        {
            if (! dragged)
            {
                if (const Note* hit = noteAt (notes, p.tick, p.pitch, settings.edgeTicks))
                {
                    if (chosen.count (hit->id)) chosen.erase (hit->id);
                    else chosen.insert (hit->id);
                }
                o.action = Action::Select;
                o.selection = chosen;
                o.selectionChanged = true;
                break;
            }
            o = areaOutcome (p, Action::Select);
            chosen = o.notes;
            o.selection = chosen;
            o.selectionChanged = true;
            o.showArea = false;
            break;
        }
        case Mode::Adding:
            o = last;
            o.action = Action::AddNote;
            o.audition = false;
            break;
        case Mode::Moving:
        case Mode::ResizingStart:
        case Mode::ResizingEnd:
            if (! dragged)
            {
                o.action = Action::Select;
                o.selection = chosen;
                o.selectionChanged = last.selectionChanged;
            }
            else
            {
                o = last;
                o.audition = false;
                if ((o.action == Action::MoveNotes || o.action == Action::CopyNotes) && o.dt == 0 && o.dPitch == 0)
                    o.action = Action::Select;
                if (o.action == Action::ResizeNotes && o.dStart == 0 && o.dEnd == 0)
                    o.action = Action::Select;
            }
            break;
        case Mode::Idle:
            break;
    }
    current = Mode::Idle;
    return o;
}

} // namespace kal::roll
