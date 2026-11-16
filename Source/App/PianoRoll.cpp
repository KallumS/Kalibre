#include "PianoRoll.h"

#include "Mpe.h"

#include "Harmony.h"

namespace kal
{
using namespace juce;

static bool isBlack (int pitch)
{
    const int pc = pitch % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

PianoRoll::PianoRoll (Controller& ctrl) : c (ctrl), lanes (ctrl)
{
    addAndMakeVisible (lanes);
    lanes.xOf = [this] (Tick t) { return xOf (t); };
    lanes.tickAt = [this] (float x) { return relAt (x); };
    setWantsKeyboardFocus (true);
    setTitle ("Piano roll");
    setDescription ("Click to add a note, right-click to remove one, drag to move or stretch. Shift-drag chooses, right-drag removes.");
    timeline.pixelsPerTick = 0.12;
    c.addChangeListener (this);
    startTimerHz (30);
}

PianoRoll::~PianoRoll() { c.removeChangeListener (this); }

int PianoRoll::articulationHeight() const
{
    const auto* t = const_cast<Controller&> (c).focus();
    return t && c.project.articulationMap (t->articulationMap) ? theme::px (22) : 0;
}

int PianoRoll::ccHeight() const
{
    // At most half of what the notes would have.
    return lanes.heightFor (jmax (0, (getHeight() - rulerHeight() - velocityHeight() - articulationHeight()) / 2));
}

Rectangle<int> PianoRoll::noteArea() const
{
    return getLocalBounds().withTrimmedLeft (keyboardWidth()).withTrimmedTop (rulerHeight())
                           .withTrimmedBottom (velocityHeight() + articulationHeight() + ccHeight());
}

Rectangle<int> PianoRoll::velocityArea() const
{
    return getLocalBounds().withTrimmedLeft (keyboardWidth()).withTrimmedBottom (ccHeight()).removeFromBottom (velocityHeight());
}

Rectangle<int> PianoRoll::articulationArea() const
{
    return getLocalBounds().withTrimmedLeft (keyboardWidth()).withTrimmedBottom (velocityHeight() + ccHeight()).removeFromBottom (articulationHeight());
}

Tick PianoRoll::base() const
{
    const auto* clip = c.project.clip (c.selectedClip);
    return clip ? clip->start - clip->offset : 0;
}

float PianoRoll::xOf (Tick rel) const { return (float) keyboardWidth() + timeline.x (base() + rel); }
Tick PianoRoll::relAt (float x) const { return timeline.tick (x - (float) keyboardWidth()) - base(); }
float PianoRoll::yOf (int pitch) const { return (float) rulerHeight() + (scrollPitch - (float) pitch) * rowHeight; }
int PianoRoll::pitchAt (float y) const { return jlimit (0, 127, (int) std::ceil (scrollPitch - (y - (float) rulerHeight()) / rowHeight)); }

void PianoRoll::showClip()
{
    const auto* clip = c.project.clip (c.selectedClip);
    shownClip = c.selectedClip;
    if (! clip) { repaint(); return; }
    const auto area = noteArea();
    if (area.getWidth() > 0)
    {
        timeline.pixelsPerTick = jlimit (0.005, 2.0, (double) area.getWidth() * 0.95 / (double) jmax<Tick> (PPQ, clip->length));
        timeline.startTick = (double) clip->start - (double) PPQ * 0.0;
    }
    int lo = 127, hi = 0;
    for (auto& n : clip->midi.notes) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }
    if (lo > hi) { lo = 48; hi = 72; }
    const float rows = (float) area.getHeight() / rowHeight;
    scrollPitch = jlimit (rows, 127.0f, (float) (lo + hi) * 0.5f + rows * 0.5f);
    repaint();
}

void PianoRoll::resized()
{
    if (shownClip != c.selectedClip) showClip();
    lanes.gutter = keyboardWidth();
    lanes.setBounds (getLocalBounds().removeFromBottom (ccHeight()));
}

void PianoRoll::changeListenerCallback (ChangeBroadcaster*)
{
    if (shownClip != c.selectedClip) showClip();
    lanes.setBounds (getLocalBounds().removeFromBottom (ccHeight()));
    lanes.repaint();
    repaint();
}

void PianoRoll::timerCallback()
{
    if (c.engine.isPlaying()) repaint (noteArea());
}

//==============================================================================
void PianoRoll::paint (Graphics& g)
{
    g.fillAll (theme::roll);
    const auto* clip = c.project.clip (c.selectedClip);
    const auto area = noteArea();
    const auto& scale = c.project.scale;
    const auto* track = const_cast<Controller&> (c).focus();

    // Rows: the scale's notes shaded, its root more; otherwise the keyboard's colours.
    for (int p = 0; p < 128; ++p)
    {
        const float y = yOf (p);
        if (y > (float) area.getBottom() || y + rowHeight < (float) area.getY()) continue;
        Colour row = isBlack (p) ? theme::rowBlack : theme::rowWhite;
        if (scale.highlight)
        {
            if (harmony::isRoot (scale, p)) row = theme::rowRoot;
            else if (harmony::inScale (scale, p)) row = theme::rowInScale;
            else row = theme::rowBlack;
        }
        g.setColour (row);
        g.fillRect ((float) area.getX(), y, (float) area.getWidth(), rowHeight);
        if (p % 12 == 0)
        {
            g.setColour (theme::rule);
            g.fillRect ((float) area.getX(), y + rowHeight - 1, (float) area.getWidth(), 1.0f);
        }
    }

    // Grid: bars, beats, grid steps; all counted from each bar line.
    const auto& tempo = c.project.tempo;
    const Tick from = base() + relAt ((float) area.getX()), to = base() + relAt ((float) area.getRight());
    for (int bar = std::max (0, tempo.barAt (std::max<Tick> (0, from))); tempo.barStart (bar) <= to; ++bar)
    {
        const auto& m = tempo.meterAtBar (bar);
        const Tick start = tempo.barStart (bar);
        Tick step = c.grid;
        while ((float) (step * timeline.pixelsPerTick) < 6.0f) step *= 2;
        for (Tick t = start; t < start + m.barTicks(); t += step)
        {
            const bool beat = (t - start) % m.beatTicks() == 0;
            g.setColour (t == start ? theme::edge.withAlpha (0.6f) : beat ? theme::rule : theme::lane);
            g.fillRect ((float) keyboardWidth() + timeline.x (t), (float) area.getY(), 1.0f, (float) area.getHeight());
        }
        g.setColour (theme::text);
        g.setFont (theme::font (theme::smallPoints));
        g.drawText (String (bar + 1), (int) ((float) keyboardWidth() + timeline.x (start)) + 3, 0, 40, rulerHeight(), Justification::centredLeft, false);
    }
    g.setColour (theme::ground);
    g.fillRect (0, 0, keyboardWidth(), rulerHeight());

    if (! clip || ! clip->isMidi())
    {
        g.setColour (theme::textSecondary);
        g.setFont (theme::font());
        g.drawFittedText (clip ? "This is an audio clip. Choose a MIDI clip to edit its notes."
                               : "Choose a MIDI clip, or double-click an empty lane to make one.",
                          area.reduced (20), Justification::centred, 3);
    }
    else
    {
        // Outside the clip, faint.
        g.setColour (theme::ink.withAlpha (0.45f));
        g.fillRect (Rectangle<float> ((float) area.getX(), (float) area.getY(), jmax (0.0f, xOf (clip->offset) - (float) area.getX()), (float) area.getHeight()));
        g.fillRect (Rectangle<float> (xOf (clip->offset + clip->length), (float) area.getY(), (float) area.getRight(), (float) area.getHeight()));

        g.saveState();
        g.reduceClipRegion (area);
        const auto& notes = clip->midi.notes;
        const auto trackCol = track ? Colour (track->colour) : theme::control;
        for (auto& n : notes)
        {
            Note shown = n;
            bool moving = false;
            if (preview.notes.count (n.id))
            {
                if (preview.action == roll::Action::MoveNotes) { shown.start += preview.dt; shown.pitch = jlimit (0, 127, shown.pitch + preview.dPitch); moving = true; }
                if (preview.action == roll::Action::ResizeNotes) { shown.start += preview.dStart; shown.length += preview.dEnd - preview.dStart; }
            }
            const bool chosen = c.selectedNotes.count (n.id) > 0 || (gesture && gesture->selection().count (n.id) && preview.action != roll::Action::Select);
            const bool doomed = preview.action == roll::Action::RemoveNotes && preview.notes.count (n.id);
            auto r = Rectangle<float> (xOf (shown.start), yOf (shown.pitch) + 1.0f, jmax (3.0f, xOf (shown.end()) - xOf (shown.start) - 1.0f), rowHeight - 2.0f);
            const float shade = 0.55f + 0.45f * (float) shown.velocity / 127.0f;
            g.setColour (doomed ? theme::warn.withAlpha (0.5f) : chosen ? theme::accent : trackCol.withMultipliedBrightness (shade));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (theme::ink);
            g.drawRoundedRectangle (r, 2.0f, 1.0f);
            if (chosen && r.getWidth() > 4)
            {
                // Chosen is shown by shape as well: a bar along the note's foot.
                g.fillRect (r.getX() + 2, r.getBottom() - 2.5f, r.getWidth() - 4, 1.5f);
            }
            if (rowHeight >= 11 && r.getWidth() > 26)
            {
                g.setColour (theme::ink);
                g.setFont (theme::font (jmin (11.0f, rowHeight - 2)));
                g.drawText (String (harmony::noteName (scale, shown.pitch)), r.reduced (3, 0).toNearestInt(), Justification::centredLeft, false);
            }
            if (moving && preview.action == roll::Action::CopyNotes) {}
        }
        // Copies being dragged: drawn where they'll land.
        if (preview.action == roll::Action::CopyNotes)
            for (auto& n : notes)
                if (preview.notes.count (n.id))
                {
                    auto r = Rectangle<float> (xOf (n.start + preview.dt), yOf (n.pitch + preview.dPitch) + 1.0f, jmax (3.0f, xOf (n.end()) - xOf (n.start) - 1.0f), rowHeight - 2.0f);
                    g.setColour (theme::accent.withAlpha (0.6f));
                    g.fillRoundedRectangle (r, 2.0f);
                }
        if (preview.action == roll::Action::AddNote && gesture)
        {
            auto& n = preview.added;
            g.setColour (theme::accent);
            g.fillRoundedRectangle (Rectangle<float> (xOf (n.start), yOf (n.pitch) + 1.0f, jmax (3.0f, xOf (n.end()) - xOf (n.start) - 1.0f), rowHeight - 2.0f), 2.0f);
        }
        if (preview.showArea)
        {
            auto r = Rectangle<float>::leftTopRightBottom (xOf (preview.areaFrom), yOf (preview.areaHi), xOf (preview.areaTo), yOf (preview.areaLo) + rowHeight);
            g.setColour ((preview.action == roll::Action::RemoveNotes ? theme::warn : theme::accent).withAlpha (0.15f));
            g.fillRect (r);
            g.setColour (preview.action == roll::Action::RemoveNotes ? theme::warn : theme::accent);
            g.drawRect (r, 1.0f);
        }
        g.restoreState();

        // Velocity lane.
        auto va = velocityArea();
        g.setColour (theme::sunken);
        g.fillRect (va);
        g.setColour (theme::rule);
        g.fillRect (va.getX(), va.getY(), va.getWidth(), 1);
        if (laneKind >= 200)
        {
            // Each note's own expression: a line from its start to its end (decision 0042).
            for (auto& n : notes)
            {
                if (n.expression.empty() && c.selectedNotes.count (n.id) == 0) continue;
                Path line;
                const int steps = jmax (2, (int) ((xOf (n.end()) - xOf (n.start)) / 3.0f));
                for (int k = 0; k <= steps; ++k)
                {
                    const Tick at = n.length * k / steps;
                    const auto e = mpe::expressionAt (n, at);
                    const double v = laneKind == 200 ? 0.5 + e.bend / 24.0 : laneKind == 201 ? e.pressure : e.timbre;
                    const float x = xOf (n.start + at);
                    const float y = (float) va.getBottom() - 3 - (float) (va.getHeight() - 6) * (float) jlimit (0.0, 1.0, v);
                    if (k == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
                }
                g.setColour (c.selectedNotes.count (n.id) ? theme::accent : trackCol);
                g.strokePath (line, PathStrokeType (1.5f));
            }
            if (laneKind == 200)
            {
                g.setColour (theme::rule);
                g.fillRect (va.getX(), va.getCentreY(), va.getWidth(), 1);
            }
        }
        else if (laneKind >= 0)
        {
            // A controller: a held line between its values.
            const auto type = laneKind == 128 ? EventType::PitchBend : laneKind == 129 ? EventType::ChannelPressure : EventType::Controller;
            const double full = laneKind == 128 ? 16383.0 : 127.0;
            Path line;
            bool started = false;
            float lastY = 0;
            for (auto& ev : clip->midi.events)
            {
                if (ev.type != type || (type == EventType::Controller && ev.number != laneKind)) continue;
                const float x = xOf (ev.tick);
                const float y = (float) va.getBottom() - 3 - (float) (va.getHeight() - 6) * (float) (ev.value / full);
                if (! started) { line.startNewSubPath ((float) va.getX(), y); started = true; }
                else line.lineTo (x, lastY);
                line.lineTo (x, y);
                lastY = y;
                g.setColour (trackCol);
                g.fillEllipse (x - 2.5f, y - 2.5f, 5.0f, 5.0f);
            }
            if (started) line.lineTo ((float) va.getRight(), lastY);
            g.setColour (trackCol);
            g.strokePath (line, PathStrokeType (1.5f));
            if (laneKind == 128)
            {
                g.setColour (theme::rule);
                g.fillRect (va.getX(), va.getCentreY(), va.getWidth(), 1);
            }
        }
        else
        for (auto& n : notes)
        {
            const float x = xOf (n.start);
            if (x < (float) va.getX() || x > (float) va.getRight()) continue;
            const float h = (float) (va.getHeight() - 6) * (float) n.velocity / 127.0f;
            const bool chosen = c.selectedNotes.count (n.id) > 0;
            g.setColour (chosen ? theme::accent : trackCol);
            g.fillRect (x, (float) va.getBottom() - h - 2, 3.0f, h);
            g.fillEllipse (x - 2.0f, (float) va.getBottom() - h - 5, 7.0f, 7.0f);
        }

        // Articulation lane.
        if (articulationHeight() > 0 && track)
        {
            auto aa = articulationArea();
            g.setColour (theme::ground);
            g.fillRect (aa);
            const auto* map = c.project.articulationMap (track->articulationMap);
            g.setFont (theme::font (theme::smallPoints));
            for (auto& n : notes)
            {
                if (n.articulation < 0 || ! map || n.articulation >= (int) map->articulations.size()) continue;
                const auto& art = map->articulations[(size_t) n.articulation];
                auto r = Rectangle<float> (xOf (n.start), (float) aa.getY() + 2, jmax (16.0f, xOf (n.end()) - xOf (n.start) - 1), (float) aa.getHeight() - 4);
                g.setColour (art.colour ? Colour (art.colour) : theme::raised);
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (theme::text);
                g.drawFittedText (String (art.name), r.toNearestInt().reduced (3, 0), Justification::centredLeft, 1, 0.6f);
            }
        }
    }

    // The keyboard.
    auto kb = Rectangle<int> (0, rulerHeight(), keyboardWidth(), area.getHeight());
    g.saveState();
    g.reduceClipRegion (kb);
    for (int p = 0; p < 128; ++p)
    {
        const float y = yOf (p);
        if (y > (float) kb.getBottom() || y + rowHeight < (float) kb.getY()) continue;
        const bool black = isBlack (p);
        g.setColour (black ? theme::ink : theme::controlHover);
        g.fillRect (0.0f, y, (float) keyboardWidth() - (black ? 18.0f : 0.0f), rowHeight - 0.5f);
        if (p % 12 == 0 || (rowHeight >= 14 && ! black))
        {
            g.setColour (black ? theme::text : theme::ink);
            g.setFont (theme::font (jmin (11.0f, rowHeight)));
            g.drawText (String (harmony::noteName (scale, p)), Rectangle<float> (2.0f, y, (float) keyboardWidth() - 6, rowHeight).toNearestInt(), Justification::centredRight, false);
        }
    }
    g.restoreState();
    g.setColour (theme::ground);
    g.fillRect (0, lanesTop() - velocityHeight() - articulationHeight(), keyboardWidth(), velocityHeight() + articulationHeight());
    g.setColour (theme::textSecondary);
    g.setFont (theme::font (theme::smallPoints));
    g.drawFittedText (laneName() + ui::u8 (" \xe2\x96\xbe"), Rectangle<int> (0, lanesTop() - velocityHeight(), keyboardWidth(), velocityHeight()).reduced (4), Justification::topLeft, 3);
    if (articulationHeight() > 0)
        g.drawText ("Artic.", Rectangle<int> (0, lanesTop() - velocityHeight() - articulationHeight(), keyboardWidth(), articulationHeight()).reduced (4, 0), Justification::centredLeft, false);

    // The playhead.
    if (c.engine.isPlaying() && clip)
    {
        const float x = (float) keyboardWidth() + timeline.x (c.playhead());
        if (x >= (float) area.getX())
        {
            g.setColour (theme::playhead);
            g.fillRect (x - 0.5f, 0.0f, 2.0f, (float) getHeight());
        }
    }
    if (hasKeyboardFocus (false))
        theme::drawFocusRing (g, getLocalBounds().toFloat(), 2.0f);
}

//==============================================================================
roll::Pointer PianoRoll::pointerFor (const MouseEvent& e) const
{
    roll::Pointer p;
    p.tick = relAt ((float) e.x);
    p.pitch = pitchAt ((float) e.y);
    p.right = e.mods.isPopupMenu();
    p.shift = e.mods.isShiftDown();
    p.option = e.mods.isAltDown();
    p.command = e.mods.isCommandDown();
    return p;
}

roll::Settings PianoRoll::settings() const
{
    roll::Settings s;
    s.edgeTicks = timeline.ticksFor ((float) theme::px (6));
    // As long as the last note added, clicked or stretched; the grid until there's one.
    s.defaultLength = lastLength > 0 ? lastLength : c.grid;
    s.minLength = c.snap ? c.grid : 1;
    const Tick b = base();
    if (c.snap)
    {
        s.snap = [this, b] (Tick t) { return c.snapTick (t + b) - b; };
        s.snapDown = [this, b] (Tick t) { return c.snapDown (t + b) - b; };
    }
    return s;
}

void PianoRoll::mouseDown (const MouseEvent& e)
{
    grabKeyboardFocus();
    auto* clip = c.clip();
    if (! clip || ! clip->isMidi()) return;
    downAt = e.getPosition();
    pastThreshold = false;

    if (e.y < rulerHeight()) { c.locate (c.snapTick (base() + relAt ((float) e.x))); return; }
    if (e.x < keyboardWidth() && e.y >= lanesTop() - velocityHeight() && e.y < lanesTop()) { laneMenu(); return; }
    if (e.x < keyboardWidth())
    {
        c.engine.audition (c.focusTrack, pitchAt ((float) e.y), 100, 0.4);
        return;
    }
    if (velocityArea().contains (e.getPosition()))
    {
        velocityDrag = true;
        velocityBefore = c.project;
        lastLaneTick = -1;
        if (laneKind >= 0) drawLaneValue (e); else setVelocityAt (e);
        return;
    }
    if (articulationArea().contains (e.getPosition()))
    {
        const Tick at = relAt ((float) e.x);
        for (auto& n : clip->midi.notes)
            if (at >= n.start && at < std::max (n.end(), n.start + timeline.ticksFor (16.0f))) { articulationMenu (n); return; }
        return;
    }

    notesCopy = clip->midi.notes;
    gesture = std::make_unique<roll::Gesture> (notesCopy, c.selectedNotes, settings(), noteAnchor);
    preview = gesture->press (pointerFor (e));
    if (preview.audition && preview.auditionPitch >= 0) c.engine.audition (c.focusTrack, preview.auditionPitch, 100, 0.25);
    if (preview.selectionChanged) { c.selectedNotes = preview.selection; c.viewChanged(); }
    repaint();
}

void PianoRoll::mouseDrag (const MouseEvent& e)
{
    if (velocityDrag) { if (laneKind >= 0) drawLaneValue (e); else setVelocityAt (e); return; }
    if (e.y < rulerHeight() && ! gesture) { c.locate (c.snapTick (base() + relAt ((float) e.x))); return; }
    if (! gesture) return;
    if (! pastThreshold && e.getPosition().getDistanceFrom (downAt) < 3) return;
    pastThreshold = true;
    const int before = preview.dPitch;
    preview = gesture->drag (pointerFor (e));
    // Hearing the pitch while dragging a note up and down.
    if (preview.action == roll::Action::MoveNotes && preview.dPitch != before && ! preview.notes.empty())
        for (auto& n : notesCopy)
            if (preview.notes.count (n.id)) { c.engine.audition (c.focusTrack, n.pitch + preview.dPitch, 90, 0.15); break; }
    // The view scrolls when dragging past its edge.
    const auto area = noteArea();
    if (e.y < area.getY()) scrollPitch = jmin (127.0f, scrollPitch + 1);
    if (e.y > area.getBottom()) scrollPitch = jmax ((float) area.getHeight() / rowHeight, scrollPitch - 1);
    repaint();
}

void PianoRoll::mouseUp (const MouseEvent& e)
{
    if (velocityDrag)
    {
        velocityDrag = false;
        if (! (velocityBefore == c.project)) c.history.push (velocityBefore, laneKind >= 0 ? "Draw " + laneName().toStdString() : std::string ("Velocity"));
        c.refresh();
        return;
    }
    if (! gesture) return;
    const auto out = gesture->release (pointerFor (e));
    noteAnchor = gesture->anchor();   // where the next Shift-click range starts (decision 0053)
    if (out.noteLength > 0) lastLength = out.noteLength;
    gesture.reset();
    preview = {};
    commit (out);
    repaint();
}

void PianoRoll::commit (const roll::Outcome& o)
{
    const Id clip = c.selectedClip;
    switch (o.action)
    {
        case roll::Action::AddNote:
        {
            Id made = 0;
            auto note = o.added;
            c.apply ("Add Note", [&] (Project& p) { made = edit::addNote (p, clip, note); });
            c.selectedNotes = { made };
            // A note drawn past the clip's end lengthens the clip.
            if (auto* cl = c.clip())
                if (note.end() > cl->offset + cl->length)
                {
                    const Tick end = c.project.tempo.barStart (c.project.tempo.barAt (base() + note.end() - 1) + 1) - cl->start;
                    cl->length = std::max (cl->length, end);
                    c.refresh();
                }
            break;
        }
        case roll::Action::RemoveNotes:
        {
            auto ids = o.notes;
            if (ids.empty()) break;
            c.apply (ids.size() == 1 ? "Remove Note" : "Remove Notes", [&] (Project& p) { edit::removeNotes (p, clip, ids); });
            for (auto id : ids) c.selectedNotes.erase (id);
            break;
        }
        case roll::Action::MoveNotes:
        {
            auto ids = o.notes;
            c.apply ("Move Notes", [&] (Project& p) { edit::moveNotes (p, clip, ids, o.dt, o.dPitch); });
            c.selectedNotes = ids;
            break;
        }
        case roll::Action::CopyNotes:
        {
            auto ids = o.notes;
            std::set<Id> made;
            c.apply ("Copy Notes", [&] (Project& p)
            {
                made = edit::duplicateNotes (p, clip, ids, o.dt);
                edit::moveNotes (p, clip, made, 0, o.dPitch);
            });
            c.selectedNotes = made;
            break;
        }
        case roll::Action::ResizeNotes:
        {
            auto ids = o.notes;
            const Tick minLen = c.snap ? c.grid : 1;
            c.apply ("Resize Notes", [&] (Project& p) { edit::resizeNotes (p, clip, ids, o.dStart, o.dEnd, minLen); });
            c.selectedNotes = ids;
            break;
        }
        case roll::Action::Select:
            c.selectedNotes = o.selection;
            break;
        case roll::Action::None:
            break;
    }
    c.viewChanged();
}

void PianoRoll::setVelocityAt (const MouseEvent& e)
{
    auto* clip = c.clip();
    if (! clip) return;
    const auto va = velocityArea();
    const int v = jlimit (1, 127, (int) std::round (127.0 * (va.getBottom() - 2 - e.y) / (va.getHeight() - 6)));
    // The note under the pointer, or every chosen note when the pointer is over one of them.
    const Tick at = relAt ((float) e.x);
    const Tick reach = timeline.ticksFor (4.0f);
    for (auto& n : clip->midi.notes)
        if (std::abs (n.start - at) <= reach)
        {
            if (c.selectedNotes.count (n.id))
                for (auto& m : clip->midi.notes) { if (c.selectedNotes.count (m.id)) m.velocity = v; }
            else n.velocity = v;
            break;
        }
    repaint();
}

String PianoRoll::laneName() const
{
    switch (laneKind)
    {
        case -1:  return "Velocity";
        case 1:   return "Modulation";
        case 2:   return "Breath";
        case 7:   return "Volume";
        case 11:  return "Expression";
        case 64:  return "Sustain";
        case 128: return "Pitch Bend";
        case 129: return "Pressure";
        case 200: return "Note Bend (MPE)";
        case 201: return "Note Pressure (MPE)";
        case 202: return "Note Slide (MPE)";
        default:  return "CC " + String (laneKind);
    }
}

void PianoRoll::laneMenu()
{
    PopupMenu m;
    for (int k : { -1, 1, 11, 64, 128, 129, 2, 7 })
    {
        const int saved = laneKind;
        laneKind = k;
        const auto name = laneName();
        laneKind = saved;
        m.addItem (name, true, laneKind == k, [this, k] { laneKind = k; repaint(); });
    }
    PopupMenu more;
    for (int cc = 0; cc < 128; ++cc)
        if (cc != 1 && cc != 2 && cc != 7 && cc != 11 && cc != 64)
            more.addItem ("CC " + String (cc), true, laneKind == cc, [this, cc] { laneKind = cc; repaint(); });
    m.addSubMenu ("Other Controllers", more);
    // Each note's own expression, for MPE tracks (decision 0042).
    m.addSeparator();
    for (int k : { 200, 201, 202 })
    {
        const int saved = laneKind;
        laneKind = k;
        const auto name = laneName();
        laneKind = saved;
        m.addItem (name, true, laneKind == k, [this, k] { laneKind = k; repaint(); });
    }
    m.showMenuAsync (PopupMenu::Options());
}

void PianoRoll::drawLaneValue (const MouseEvent& e)
{
    auto* clip = c.clip();
    if (! clip) return;
    const auto va = velocityArea();
    if (laneKind >= 200)
    {
        // A note's expression, for the notes sounding there (the chosen ones, if any are).
        const Tick step = jmax<Tick> (PPQ / 32, c.grid / 4);
        const Tick tick = jmax<Tick> (0, (relAt ((float) e.x) / step) * step);
        const double v = jlimit (0.0, 1.0, (double) (va.getBottom() - 3 - e.y) / (va.getHeight() - 6));
        const Tick from = lastLaneTick < 0 ? tick : jmin (lastLaneTick, tick), to = lastLaneTick < 0 ? tick : jmax (lastLaneTick, tick);
        for (auto& n : clip->midi.notes)
        {
            if (tick < n.start || tick >= n.end()) continue;
            if (! c.selectedNotes.empty() && ! c.selectedNotes.count (n.id)) continue;
            auto now = mpe::expressionAt (n, tick - n.start);
            if (laneKind == 200) now.bend = (v - 0.5) * 24.0;          // a lane's height is two octaves of bend
            else if (laneKind == 201) now.pressure = v;
            else now.timbre = v;
            auto& ex = n.expression;
            ex.erase (std::remove_if (ex.begin(), ex.end(), [&] (const NoteExpression& x)
            {
                return x.tick + n.start >= from && x.tick + n.start <= to && x.tick != lastLaneTick - n.start;
            }), ex.end());
            if (! e.mods.isPopupMenu())
            {
                now.tick = tick - n.start;
                ex.push_back (now);
                std::sort (ex.begin(), ex.end(), [] (const NoteExpression& a, const NoteExpression& b) { return a.tick < b.tick; });
            }
        }
        lastLaneTick = tick;
        repaint();
        return;
    }
    const auto type = laneKind == 128 ? EventType::PitchBend : laneKind == 129 ? EventType::ChannelPressure : EventType::Controller;
    const int full = laneKind == 128 ? 16383 : 127;
    const Tick step = jmax<Tick> (PPQ / 32, c.grid / 4);
    const Tick tick = jmax<Tick> (0, (relAt ((float) e.x) / step) * step);
    const Tick from = lastLaneTick < 0 ? tick : jmin (lastLaneTick, tick);
    const Tick to = lastLaneTick < 0 ? tick : jmax (lastLaneTick, tick);
    auto& evs = clip->midi.events;
    // What was there is replaced from the last point to this one; a right-drag only erases.
    evs.erase (std::remove_if (evs.begin(), evs.end(), [&] (const MidiEvent& ev)
    {
        return ev.type == type && (type != EventType::Controller || ev.number == laneKind) && ev.tick >= from && ev.tick <= to
            && (ev.tick != lastLaneTick || lastLaneTick == tick);   // the point just drawn stays
    }), evs.end());
    if (! e.mods.isPopupMenu())
    {
        MidiEvent ev;
        ev.tick = tick;
        ev.type = type;
        ev.number = laneKind;
        ev.channel = 0;
        ev.value = jlimit (0, full, roundToInt ((double) full * (va.getBottom() - 3 - e.y) / (va.getHeight() - 6)));
        evs.push_back (ev);
        edit::sortNotes (clip->midi);
    }
    lastLaneTick = tick;
    repaint();
}

void PianoRoll::articulationMenu (const Note& n)
{
    const auto* t = c.focus();
    const auto* map = t ? c.project.articulationMap (t->articulationMap) : nullptr;
    if (! map) return;
    std::set<Id> ids = c.selectedNotes.count (n.id) ? c.selectedNotes : std::set<Id> { n.id };
    PopupMenu m;
    m.addItem ("None", true, n.articulation < 0, [this, ids] { c.apply ("Articulation", [&] (Project& p) { edit::setArticulation (p, c.selectedClip, ids, -1); }); });
    for (size_t i = 0; i < map->articulations.size(); ++i)
        m.addItem (String (map->articulations[i].name), true, n.articulation == (int) i, [this, ids, i]
        {
            c.apply ("Articulation", [&] (Project& p) { edit::setArticulation (p, c.selectedClip, ids, (int) i); });
        });
    m.showMenuAsync (PopupMenu::Options());
}

void PianoRoll::mouseMove (const MouseEvent& e)
{
    const auto* clip = c.project.clip (c.selectedClip);
    if (! clip || ! noteArea().contains (e.getPosition())) { setMouseCursor (MouseCursor::NormalCursor); return; }
    roll::Zone zone;
    if (roll::noteAt (clip->midi.notes, relAt ((float) e.x), pitchAt ((float) e.y), timeline.ticksFor ((float) theme::px (6)), &zone))
        setMouseCursor (zone == roll::Zone::Body ? MouseCursor::DraggingHandCursor : MouseCursor::LeftRightResizeCursor);
    else setMouseCursor (MouseCursor::NormalCursor);
}

void PianoRoll::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        timeline.zoomAround ((float) (e.x - keyboardWidth()), w.deltaY > 0 ? 1.15 : 1 / 1.15);
    else if (e.mods.isAltDown())
        rowHeight = jlimit (6.0f, 28.0f, rowHeight + (w.deltaY > 0 ? 1.0f : -1.0f));
    else
    {
        if (std::abs (w.deltaX) > 0.0f || e.mods.isShiftDown())
            timeline.startTick = std::max (0.0, timeline.startTick - (e.mods.isShiftDown() ? w.deltaY : w.deltaX) * 40.0 / timeline.pixelsPerTick);
        if (! e.mods.isShiftDown())
            scrollPitch = jlimit ((float) noteArea().getHeight() / rowHeight, 127.0f, scrollPitch + w.deltaY * 12.0f);
    }
    repaint();
}

void PianoRoll::mouseMagnify (const MouseEvent& e, float s)
{
    timeline.zoomAround ((float) (e.x - keyboardWidth()), s);
    repaint();
}

bool PianoRoll::keyPressed (const KeyPress& k)
{
    auto* clip = c.clip();
    if (! clip) return false;
    const auto ids = c.selectedNotes;
    const Id cid = c.selectedClip;
    const int code = k.getKeyCode();
    const bool shift = k.getModifiers().isShiftDown();
    if (code == KeyPress::deleteKey || code == KeyPress::backspaceKey) { c.deleteSelection(); return true; }
    if (code == KeyPress::upKey || code == KeyPress::downKey)
    {
        if (ids.empty()) return false;
        const int d = (code == KeyPress::upKey ? 1 : -1) * (shift ? 12 : 1);
        c.apply ("Transpose", [=] (Project& p) { edit::transpose (p, cid, ids, d); });
        return true;
    }
    if (code == KeyPress::leftKey || code == KeyPress::rightKey)
    {
        if (ids.empty()) return false;
        const Tick d = (code == KeyPress::rightKey ? 1 : -1) * (shift ? c.project.tempo.meterAt (c.cursor).barTicks() : c.grid);
        c.apply ("Move Notes", [=] (Project& p) { edit::moveNotes (p, cid, ids, d, 0); });
        return true;
    }
    if (code == KeyPress::escapeKey)
    {
        if (! c.selectedNotes.empty()) { c.selectedNotes.clear(); c.viewChanged(); return true; }
        giveAwayKeyboardFocus();
        return true;
    }
    return false;
}

} // namespace kal
