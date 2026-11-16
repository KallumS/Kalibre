#include "CcLanes.h"

#include "Controllers.h"

namespace kal
{
using namespace juce;

static const Track* ownerOf (const Controller& c)
{
    const Track* owner = nullptr;
    c.project.clip (c.selectedClip, &owner);
    return owner;
}

CcLanes::CcLanes (Controller& ctrl) : c (ctrl)
{
    setTitle ("Controller lanes");
    setDescription ("Drag in a lane to draw its values; right-drag to clear. Click a lane's name to change or remove it.");
}

std::vector<int> CcLanes::shown() const
{
    std::vector<int> out;
    const auto* clip = c.project.clip (c.selectedClip);
    if (! clip || ! clip->isMidi()) return out;
    if (const auto* t = ownerOf (c)) for (int cc : t->ccLanes) if (cc >= 0 && cc < 128 && std::find (out.begin(), out.end(), cc) == out.end()) out.push_back (cc);
    std::set<int> used;
    for (auto& e : clip->midi.events) if (e.type == EventType::Controller) used.insert (e.number);
    for (int cc : used) if (std::find (out.begin(), out.end(), cc) == out.end()) out.push_back (cc);
    return out;
}

int CcLanes::heightFor (int room) const
{
    const int n = (int) shown().size();
    if (n == 0 || room <= 0) return 0;
    const int each = jlimit (theme::px (24), theme::px (44), room / n);
    return jmin (room, each * n);
}

Rectangle<int> CcLanes::laneArea (int index) const
{
    const int n = jmax (1, (int) shown().size());
    const int h = getHeight() / n;
    return { 0, index * h, getWidth(), h };
}

int CcLanes::laneAt (int y) const
{
    const int n = (int) shown().size();
    if (n == 0) return -1;
    return jlimit (0, n - 1, y / jmax (1, getHeight() / n));
}

void CcLanes::paint (Graphics& g)
{
    const auto* clip = c.project.clip (c.selectedClip);
    const auto lanes = shown();
    if (! clip || lanes.empty() || ! xOf) return;
    for (int i = 0; i < (int) lanes.size(); ++i)
    {
        const int cc = lanes[(size_t) i];
        const auto r = laneArea (i);
        auto values = r.withTrimmedLeft (gutter).reduced (0, 3);
        g.setColour (theme::roll);
        g.fillRect (r.withTrimmedLeft (gutter));
        g.setColour (theme::sunken);
        g.fillRect (r.withWidth (gutter));
        g.setColour (theme::rule);
        g.fillRect (r.getX(), r.getY(), r.getWidth(), 1);
        g.fillRect ((float) values.getX(), (float) values.getCentreY(), (float) values.getWidth(), 1.0f);   // the middle, 64

        // Its name: the number, and what the standard calls it.
        auto name = r.withWidth (gutter).reduced (theme::px (4), 2);
        g.setColour (theme::text);
        g.setFont (theme::font (theme::smallPoints, true));
        g.drawText ("CC " + String (cc) + ui::u8 (" \xe2\x96\xbe"), name.removeFromTop (theme::px (14)), Justification::topLeft, false);
        const auto what = controllers::name (cc);
        if (what != "Undefined" && name.getHeight() >= theme::px (12))
        {
            g.setColour (theme::textSecondary);
            g.setFont (theme::font (theme::smallPoints));
            g.drawFittedText (String (what), name, Justification::topLeft, 2, 0.8f);
        }

        // Its values: each holds until the next.
        g.saveState();
        g.reduceClipRegion (values.withY (r.getY()).withHeight (r.getHeight()));
        auto yOf = [&] (int v) { return (float) values.getBottom() - (float) values.getHeight() * (float) v / 127.0f; };
        Path line;
        bool started = false;
        float lastY = 0;
        for (auto& e : clip->midi.events)
        {
            if (e.type != EventType::Controller || e.number != cc) continue;
            const float x = xOf (e.tick), y = yOf (e.value);
            if (! started) { line.startNewSubPath ((float) values.getX(), y); started = true; }
            else line.lineTo (x, lastY);
            line.lineTo (x, y);
            lastY = y;
        }
        if (started)
        {
            line.lineTo ((float) values.getRight(), lastY);
            Path fill (line);
            fill.lineTo ((float) values.getRight(), (float) values.getBottom());
            fill.lineTo ((float) values.getX(), (float) values.getBottom());
            fill.closeSubPath();
            g.setColour (theme::control.withAlpha (0.18f));
            g.fillPath (fill);
            g.setColour (theme::control);
            g.strokePath (line, PathStrokeType (1.5f));
        }
        g.restoreState();
    }
}

void CcLanes::mouseDown (const MouseEvent& e)
{
    const auto lanes = shown();
    if (lanes.empty() || ! c.clip()) return;
    const int i = laneAt (e.y);
    if (i < 0) return;
    if (e.x < gutter) { laneMenu (lanes[(size_t) i]); return; }
    before = c.project;
    drawing = lanes[(size_t) i];
    lastTick = -1;
    drawAt (e);
}

void CcLanes::mouseDrag (const MouseEvent& e)
{
    if (drawing >= 0) drawAt (e);
}

void CcLanes::mouseUp (const MouseEvent&)
{
    if (drawing < 0) return;
    drawing = -1;
    lastTick = -1;
    if (! (before == c.project)) c.history.push (before, "Draw CC");
    c.refresh();
}

// What was there is replaced from the last point to this one; a right-drag only clears.
void CcLanes::drawAt (const MouseEvent& e)
{
    auto* clip = c.clip();
    if (! clip || ! tickAt) return;
    const auto lanes = shown();
    const auto it = std::find (lanes.begin(), lanes.end(), drawing);
    if (it == lanes.end()) return;
    const auto values = laneArea ((int) (it - lanes.begin())).withTrimmedLeft (gutter).reduced (0, 3);
    const Tick step = jmax<Tick> (PPQ / 32, c.grid / 4);
    const Tick tick = jlimit<Tick> (clip->offset, clip->offset + clip->length - 1, (tickAt ((float) e.x) / step) * step);
    const Tick from = lastTick < 0 ? tick : jmin (lastTick, tick), to = lastTick < 0 ? tick : jmax (lastTick, tick);
    auto& evs = clip->midi.events;
    evs.erase (std::remove_if (evs.begin(), evs.end(), [&] (const MidiEvent& ev)
    {
        return ev.type == EventType::Controller && ev.number == drawing && ev.tick >= from && ev.tick <= to
            && (ev.tick != lastTick || lastTick == tick);   // the point just drawn stays
    }), evs.end());
    if (! e.mods.isPopupMenu())
    {
        MidiEvent ev;
        ev.tick = tick;
        ev.type = EventType::Controller;
        ev.number = drawing;
        ev.value = jlimit (0, 127, roundToInt (127.0 * (values.getBottom() - e.y) / jmax (1, values.getHeight())));
        evs.push_back (ev);
        edit::sortNotes (clip->midi);
    }
    lastTick = tick;
    repaint();
}

void CcLanes::laneMenu (int cc)
{
    const Id cid = c.selectedClip;
    PopupMenu m;
    m.addSectionHeader (controllers::label (cc));
    m.addItem ("Clear Lane", [this, cid, cc]
    {
        c.apply ("Clear Lane", [=] (Project& p)
        {
            if (auto* cl = p.clip (cid))
                cl->midi.events.erase (std::remove_if (cl->midi.events.begin(), cl->midi.events.end(), [cc] (const MidiEvent& e)
                {
                    return e.type == EventType::Controller && e.number == cc;
                }), cl->midi.events.end());
        });
    });
    // Hiding a lane keeps its values; a lane with values in the clip stays shown.
    m.addItem ("Remove Lane", [this, cid, cc]
    {
        c.apply ("Remove Lane", [=] (Project& p)
        {
            Track* t = nullptr;
            p.clip (cid, &t);
            if (t) t->ccLanes.erase (std::remove (t->ccLanes.begin(), t->ccLanes.end(), cc), t->ccLanes.end());
        });
    });
    m.addSeparator();
    m.addSubMenu ("Show Lanes", lanesOf (c));
    m.showMenuAsync (PopupMenu::Options());
}

void CcLanes::lanesMenu (Controller& c, Component* target)
{
    if (! ownerOf (c)) { c.say ("Choose a MIDI clip to show its controller lanes."); return; }
    lanesOf (c).showMenuAsync (target ? PopupMenu::Options().withTargetComponent (target) : PopupMenu::Options());
}

PopupMenu CcLanes::lanesOf (Controller& c)
{
    PopupMenu m;
    const Track* owner = ownerOf (c);
    if (! owner) return m;
    const Id tid = owner->id;
    auto shownOn = [owner] (int cc) { return std::find (owner->ccLanes.begin(), owner->ccLanes.end(), cc) != owner->ccLanes.end(); };
    auto toggle = [&c, tid] (int cc)
    {
        return [&c, tid, cc]
        {
            c.apply ("CC Lanes", [=] (Project& p)
            {
                if (auto* t = p.track (tid))
                {
                    auto& l = t->ccLanes;
                    if (auto it = std::find (l.begin(), l.end(), cc); it != l.end()) l.erase (it);
                    else l.push_back (cc);
                }
            });
        };
    };
    for (int cc : controllers::common()) m.addItem (String (controllers::label (cc)), true, shownOn (cc), toggle (cc));
    PopupMenu all;
    for (int from = 0; from < 128; from += 32)
    {
        PopupMenu part;
        for (int cc = from; cc < from + 32; ++cc) part.addItem (String (controllers::label (cc)), true, shownOn (cc), toggle (cc));
        all.addSubMenu ("CC " + String (from) + ui::u8 ("\xe2\x80\x93") + String (from + 31), part);
    }
    m.addSeparator();
    m.addSubMenu ("Every Controller", all);
    return m;
}

} // namespace kal
