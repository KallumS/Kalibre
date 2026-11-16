#include "NotationView.h"

namespace kal
{
using namespace juce;
using notation::Value;

NotationView::NotationView (Controller& ctrl) : c (ctrl)
{
    setTitle ("Notation");
    const char* names[] { "1", "1/2", "1/4", "1/8", "1/16", "1/32" };
    const char* tips[] { "Whole note", "Half note", "Quarter note", "Eighth note", "Sixteenth note", "Thirty-second note" };
    for (int i = 0; i < 6; ++i)
    {
        auto* b = values.add (new theme::HouseButton (names[i], tips[i], false));
        b->setRadioGroupId (77);
        b->setClickingTogglesState (true);
        b->onClick = [this, i] { value = (Value) i; };
        addAndMakeVisible (b);
    }
    values[2]->setToggleState (true, dontSendNotification);
    for (auto* b : { &dotted, &triplet, &rest }) addAndMakeVisible (b);
    rest.onClick = [this]
    {
        c.locate (c.cursor + notation::ticksOf (value, dotted.getToggleState(), triplet.getToggleState()));
    };
    c.addChangeListener (this);
    changeListenerCallback (nullptr);
}

NotationView::~NotationView() { c.removeChangeListener (this); }

void NotationView::changeListenerCallback (ChangeBroadcaster*)
{
    const auto* clip = c.project.clip (c.selectedClip);
    bars.clear();
    if (clip && clip->isMidi())
    {
        clef = notation::clefFor (clip->midi.notes);
        bars = notation::layout (*clip, c.project.tempo, c.project.scale, clef);
    }
    repaint();
}

void NotationView::resized()
{
    auto top = getLocalBounds().reduced (theme::px (8)).removeFromTop (theme::px (24));
    for (auto* b : values) { b->setBounds (top.removeFromLeft (theme::px (40))); top.removeFromLeft (4); }
    top.removeFromLeft (8);
    for (auto* b : { &dotted, &triplet }) { b->setBounds (top.removeFromLeft (theme::px (30))); top.removeFromLeft (4); }
    rest.setBounds (top.removeFromLeft (rest.idealWidth (theme::smallControl)));
}

Rectangle<int> NotationView::staffArea() const
{
    return getLocalBounds().withTrimmedTop (theme::px (40)).reduced (theme::px (16), 0);
}

float NotationView::yOfStep (int step) const
{
    // Step 0 is the bottom line; the staff sits in the middle of the area.
    const auto a = staffArea();
    const float bottomLine = (float) a.getCentreY() + lineGap() * 2.0f;
    return bottomLine - (float) step * lineGap() * 0.5f;
}

int NotationView::stepAt (float y) const
{
    const auto a = staffArea();
    const float bottomLine = (float) a.getCentreY() + lineGap() * 2.0f;
    return (int) std::round ((bottomLine - y) / (lineGap() * 0.5f));
}

Tick NotationView::tickAtX (float x) const
{
    Tick best = 0;
    for (auto& [t, gx] : tickX) if (gx <= x + 4) best = t;
    return best;
}

void NotationView::paint (Graphics& g)
{
    g.fillAll (theme::roll);
    placed.clear();
    tickX.clear();
    const auto* clip = c.project.clip (c.selectedClip);
    const auto a = staffArea();
    if (! clip || ! clip->isMidi())
    {
        g.setColour (theme::textSecondary);
        g.setFont (theme::font());
        g.drawFittedText ("Choose a MIDI clip to see and write it as notation.", a, Justification::centred, 2);
        return;
    }
    // The staff.
    g.setColour (theme::edge);
    for (int line = 0; line < 5; ++line) g.fillRect ((float) a.getX(), yOfStep (line * 2), (float) a.getWidth(), 1.0f);
    g.setColour (theme::text);
    g.setFont (theme::font (28.0f));
    g.drawText (clef == notation::Clef::Treble ? String::fromUTF8 ("\xf0\x9d\x84\x9e") : String::fromUTF8 ("\xf0\x9d\x84\xa2"),
                Rectangle<float> ((float) a.getX(), yOfStep (8) - 10, 30, yOfStep (0) - yOfStep (8) + 20).toNearestInt(), Justification::centred, false);
    g.setFont (theme::font (theme::smallPoints));
    g.drawText (clef == notation::Clef::Treble ? "Treble" : "Bass", a.getX(), (int) yOfStep (-4), 60, 14, Justification::centredLeft, false);

    float x = (float) a.getX() + 40;
    const float unit = (float) theme::px (34);   // a quarter note's width at its least
    for (auto& bar : bars)
    {
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        g.drawText (String (bar.index + 1), (int) x, (int) yOfStep (12), 30, 12, Justification::centredLeft, false);
        if (bar.index == bars.front().index || bar.numerator != bars.front().numerator)
        {
            g.setColour (theme::text);
            g.setFont (theme::font (16.0f, true));
            g.drawText (String (bar.numerator), (int) x, (int) yOfStep (8) - 2, 18, (int) lineGap() * 2, Justification::centred, false);
            g.drawText (String (bar.denominator), (int) x, (int) yOfStep (4) - 2, 18, (int) lineGap() * 2, Justification::centred, false);
            x += 24;
        }
        for (auto& gl : bar.glyphs)
        {
            const float w = jmax (unit * 0.7f, unit * (float) gl.duration.ticks / (float) PPQ);
            tickX.push_back ({ gl.start, x });
            const bool filled = gl.duration.value != Value::Whole && gl.duration.value != Value::Half;
            if (gl.rest)
            {
                g.setColour (theme::textSecondary);
                g.setFont (theme::font (18.0f));
                const char* restGlyph = gl.duration.value == Value::Whole ? "\xf0\x9d\x84\xbb" : gl.duration.value == Value::Half ? "\xf0\x9d\x84\xbc"
                                      : gl.duration.value == Value::Quarter ? "\xf0\x9d\x84\xbd" : gl.duration.value == Value::Eighth ? "\xf0\x9d\x84\xbe" : "\xf0\x9d\x84\xbf";
                g.drawText (String::fromUTF8 (restGlyph), Rectangle<float> (x, yOfStep (6), 18, yOfStep (2) - yOfStep (6)).toNearestInt(), Justification::centred, false);
            }
            else
            {
                const bool chosen = std::any_of (clip->midi.notes.begin(), clip->midi.notes.end(), [&] (const Note& n)
                {
                    return c.selectedNotes.count (n.id) && n.start - clip->offset == gl.start;
                });
                g.setColour (chosen ? theme::accent : theme::text);
                float stemTop = 1e9f, stemBottom = -1e9f;
                for (size_t i = 0; i < gl.pitches.size(); ++i)
                {
                    const int step = gl.steps[i];
                    const float y = yOfStep (step);
                    // Ledger lines.
                    for (int l = step <= -2 ? -2 : 10; step <= -2 ? l >= step : l <= step; l += step <= -2 ? -2 : 2)
                        g.fillRect (x - 4, yOfStep (l), 18.0f, 1.0f);
                    auto head = Rectangle<float> (x, y - lineGap() * 0.45f, lineGap() * 1.3f, lineGap() * 0.9f);
                    if (filled) g.fillEllipse (head); else g.drawEllipse (head.reduced (0.75f), 1.5f);
                    if (gl.spelled[i].accidental != 0)
                    {
                        g.setFont (theme::font (14.0f));
                        const auto acc = gl.spelled[i].accidental;
                        g.drawText (acc > 1 ? String ("x") : acc == 1 ? String::fromUTF8 ("\xe2\x99\xaf") : acc == -1 ? String::fromUTF8 ("\xe2\x99\xad") : String::fromUTF8 ("\xe2\x99\xad\xe2\x99\xad"),
                                    Rectangle<float> (x - 13, y - 9, 12, 18).toNearestInt(), Justification::centredRight, false);
                    }
                    if (gl.duration.dotted) g.fillEllipse (x + lineGap() * 1.6f, y - 1.5f, 3.0f, 3.0f);
                    stemTop = jmin (stemTop, y);
                    stemBottom = jmax (stemBottom, y);
                    placed.push_back ({ gl.start, gl.pitches[i], head });
                }
                if (gl.duration.value != Value::Whole)
                {
                    const bool up = gl.steps.front() < 4;
                    const float sx = up ? x + lineGap() * 1.25f : x;
                    g.fillRect (sx, up ? stemTop - lineGap() * 3.5f : stemTop, 1.2f, (stemBottom - stemTop) + lineGap() * 3.5f);
                    // Flags.
                    const int flags = gl.duration.value == Value::Eighth ? 1 : gl.duration.value == Value::Sixteenth ? 2 : gl.duration.value == Value::ThirtySecond ? 3 : 0;
                    for (int f = 0; f < flags; ++f)
                    {
                        const float fy = up ? stemTop - lineGap() * 3.5f + (float) f * 5 : stemBottom + lineGap() * 3.5f - (float) f * 5;
                        g.drawLine (sx, fy, sx + 7, fy + (up ? 7.0f : -7.0f), 1.5f);
                    }
                }
                if (gl.tieToNext)
                {
                    Path tie;
                    const float ty = yOfStep (gl.steps.front()) + lineGap();
                    tie.startNewSubPath (x + lineGap(), ty);
                    tie.quadraticTo (x + w * 0.5f, ty + 6, x + w, ty);
                    g.strokePath (tie, PathStrokeType (1.0f));
                }
            }
            x += w;
        }
        g.setColour (theme::edge);
        g.fillRect (x - 6, yOfStep (8), 1.0f, yOfStep (0) - yOfStep (8));
        tickX.push_back ({ bar.start + bar.length, x });
        if (x > (float) a.getRight()) break;
    }
    // The cursor, where the next note goes.
    const Tick rel = c.cursor - (clip->start - clip->offset);
    float cx = -1;
    for (auto& [t, gx] : tickX) if (t <= rel) cx = gx;
    if (cx > 0)
    {
        g.setColour (theme::accent);
        g.fillRect (cx - 3, yOfStep (10), 2.0f, yOfStep (-2) - yOfStep (10));
    }
}

void NotationView::mouseDown (const MouseEvent& e)
{
    auto* clip = c.clip();
    if (! clip || ! clip->isMidi() || ! staffArea().contains (e.getPosition())) return;
    const Id cid = clip->id;
    if (e.mods.isPopupMenu())
    {
        for (auto& p : placed)
            if (p.r.expanded (3).contains (e.position))
            {
                std::set<Id> gone;
                for (auto& n : clip->midi.notes)
                    if (n.start - clip->offset == p.start && n.pitch == p.pitch) gone.insert (n.id);
                c.apply ("Remove Note", [=] (Project& pr) { edit::removeNotes (pr, cid, gone); });
                return;
            }
        return;
    }
    // A click on a note chooses it.
    for (auto& p : placed)
        if (p.r.expanded (2).contains (e.position))
        {
            c.selectedNotes.clear();
            for (auto& n : clip->midi.notes)
                if (n.start - clip->offset == p.start && n.pitch == p.pitch) c.selectedNotes.insert (n.id);
            c.engine.audition (c.focusTrack, p.pitch, 100, 0.3);
            c.viewChanged();
            return;
        }
    // Otherwise a new note at the cursor (or where clicked, if past the last note).
    const int step = stepAt ((float) e.y);
    const int nudge = e.mods.isAltDown() ? 1 : e.mods.isShiftDown() ? -1 : 0;
    const int pitch = notation::pitchAtStep (step, clef, c.project.scale, nudge);
    const Tick len = notation::ticksOf (value, dotted.getToggleState(), triplet.getToggleState());
    Tick at = c.cursor - (clip->start - clip->offset);
    const Tick clicked = tickAtX ((float) e.x);
    if (at < clip->offset || at >= clip->offset + clip->length) at = clicked;
    Note n;
    n.start = at;
    n.length = len;
    n.pitch = pitch;
    c.apply ("Add Note", [&] (Project& p) { edit::addNote (p, cid, n); });
    c.engine.audition (c.focusTrack, pitch, 100, 0.3);
    c.locate (clip->start - clip->offset + at + len);
}

} // namespace kal
