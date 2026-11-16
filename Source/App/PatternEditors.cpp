#include "PatternEditors.h"

#include "Harmony.h"

namespace kal
{
using namespace juce;

static void emptyMessage (Graphics& g, Rectangle<int> r, const String& text)
{
    g.setColour (theme::textSecondary);
    g.setFont (theme::font());
    g.drawFittedText (text, r.reduced (20), Justification::centred, 3);
}

// The step of a pattern `steps` long being played in the chosen clip, or -1.
// Counted from where the patterns are written (the clip's offset), round
// its loop, as they repeat.
static int playingStep (Controller& c, Tick stepTicks, int steps)
{
    const auto* clip = c.project.clip (c.selectedClip);
    if (! clip || ! c.engine.isPlaying() || steps <= 0 || stepTicks <= 0) return -1;
    const Tick at = c.playhead();
    if (at < clip->start || at >= clip->end()) return -1;
    Tick rel = at - clip->start;
    if (clip->loopLength > 0) rel %= clip->loopLength;
    return (int) ((rel % (stepTicks * steps)) / stepTicks);
}

// The column being played: lit behind its cells.
static void lightColumn (Graphics& g, Rectangle<float> column)
{
    g.setColour (theme::playhead.withAlpha (0.22f));
    g.fillRoundedRectangle (column, 4.0f);
}

//==============================================================================
DrumGrid::DrumGrid (Controller& ctrl) : c (ctrl)
{
    setTitle ("Drum grid");
    c.addChangeListener (this);
    read();
    startTimerHz (30);
}

DrumGrid::~DrumGrid() { c.removeChangeListener (this); }

void DrumGrid::timerCallback()
{
    const int now = playingStep (c, grid.stepTicks, grid.steps);
    if (now != lit) { lit = now; repaint(); }
}

int DrumGrid::stepsFor (const Clip& clip) const
{
    return jlimit (1, 64, (int) ((clip.loopLength > 0 ? clip.loopLength : clip.length) / jmax<Tick> (1, c.grid)));
}

void DrumGrid::read()
{
    const auto* clip = c.project.clip (c.selectedClip);
    grid = {};
    if (! clip || ! clip->isMidi()) return;
    grid.stepTicks = c.grid;
    grid.steps = stepsFor (*clip);
    grid.lanes = patterns::standardKit (grid.steps);
    grid = patterns::readGrid (clip->midi, grid, clip->offset, NoteFrom::Drums);
}

void DrumGrid::write (const String& label)
{
    const Id id = c.selectedClip;
    auto g = grid;
    auto doWrite = [id, g] (Project& p)
    {
        auto* clip = p.clip (id);
        if (! clip) return;
        unsigned next = p.nextId;
        patterns::writeGrid (clip->midi, g, clip->offset, clip->loopLength > 0 ? clip->loopLength : clip->length, next, NoteFrom::Drums);
        p.nextId = std::max (p.nextId, (Id) next);
    };
    if (label.isEmpty()) { doWrite (c.project); c.refresh(); }
    else c.apply (label, doWrite);
}

Rectangle<int> DrumGrid::cellArea() const { return getLocalBounds().withTrimmedLeft (nameWidth()).reduced (0, 4); }

void DrumGrid::paint (Graphics& g)
{
    g.fillAll (theme::roll);
    const auto* clip = c.project.clip (c.selectedClip);
    if (! clip || ! clip->isMidi()) { emptyMessage (g, getLocalBounds(), "Choose a MIDI clip to draw drums into."); return; }
    const auto area = cellArea();
    const int lanes = (int) grid.lanes.size();
    if (lanes == 0) return;
    const float rowH = (float) theme::px (24);
    const float colW = (float) area.getWidth() / (float) grid.steps;
    const int stepsPerBeat = (int) jmax<Tick> (1, c.project.tempo.meterAt (clip->start).beatTicks() / grid.stepTicks);
    // Where the playhead is, step-wise.
    const int playing = playingStep (c, grid.stepTicks, grid.steps);
    if (playing >= 0)
        lightColumn (g, Rectangle<float> ((float) area.getX() + (float) playing * colW, (float) area.getY(), colW, rowH * (float) lanes));
    for (int l = 0; l < lanes; ++l)
    {
        const auto& lane = grid.lanes[(size_t) l];
        const float y = (float) area.getY() + (float) l * rowH;
        g.setColour (theme::text);
        g.setFont (theme::font (theme::smallPoints));
        g.drawText (lane.name, Rectangle<float> (6.0f, y, (float) nameWidth() - 10.0f, rowH).toNearestInt(), Justification::centredLeft, true);
        for (int s = 0; s < grid.steps; ++s)
        {
            auto cell = Rectangle<float> ((float) area.getX() + (float) s * colW, y, colW, rowH).reduced (1.5f);
            const int v = lane.velocity[(size_t) s];
            const bool beat = s % stepsPerBeat == 0;
            g.setColour (v > 0 ? theme::accent.withMultipliedBrightness (0.55f + 0.45f * (float) v / 127.0f)
                               : (beat ? theme::raised : theme::sunken));
            g.fillRoundedRectangle (cell, 3.0f);
            if (v > 0)
            {
                g.setColour (theme::ink);
                const float h = (cell.getHeight() - 4) * (float) v / 127.0f;
                g.fillRect (cell.getX() + 2, cell.getBottom() - 2 - h, 2.0f, h);   // velocity, as a shape too
            }
            if (s == playing)
            {
                g.setColour (theme::playhead);
                g.drawRoundedRectangle (cell, 3.0f, 1.5f);
            }
        }
    }
}

void DrumGrid::mouseDown (const MouseEvent& e)
{
    const auto area = cellArea();
    const int lanes = (int) grid.lanes.size();
    if (lanes == 0 || ! area.contains (e.getPosition())) return;
    const float rowH = (float) theme::px (24);
    const int l = (int) ((float) (e.y - area.getY()) / rowH);
    const int s = (int) ((float) (e.x - area.getX()) / ((float) area.getWidth() / (float) grid.steps));
    if (l < 0 || l >= lanes || s < 0 || s >= grid.steps) return;
    auto& v = grid.lanes[(size_t) l].velocity[(size_t) s];
    before = c.project;
    if (e.mods.isPopupMenu()) v = 0;
    else if (v > 0 && ! e.mods.isShiftDown()) { dragLane = l; dragStep = s; dragStartVelocity = v; return; }
    else
    {
        v = e.mods.isShiftDown() ? 127 : 100;
        c.engine.audition (c.focusTrack, grid.lanes[(size_t) l].pitch, v, 0.2);
    }
    write ("Drum Pattern");
    repaint();
}

void DrumGrid::mouseDrag (const MouseEvent& e)
{
    if (dragLane < 0) return;
    grid.lanes[(size_t) dragLane].velocity[(size_t) dragStep] = jlimit (1, 127, dragStartVelocity - e.getDistanceFromDragStartY());
    write ({});
    repaint();
}

void DrumGrid::mouseUp (const MouseEvent& e)
{
    if (dragLane >= 0)
    {
        if (e.getDistanceFromDragStart() < 3)
        {
            // A click on a hit (not a drag) clears it.
            grid.lanes[(size_t) dragLane].velocity[(size_t) dragStep] = 0;
            write ({});
        }
        if (! (before == c.project)) c.history.push (before, "Drum Pattern");
        c.refresh();
    }
    dragLane = -1;
}

void DrumGrid::changeListenerCallback (ChangeBroadcaster*)
{
    read();
    repaint();
}

//==============================================================================
StepSequencer::StepSequencer (Controller& ctrl) : c (ctrl)
{
    setTitle ("Step sequencer");
    c.addChangeListener (this);
    read();
    startTimerHz (30);
}

StepSequencer::~StepSequencer() { c.removeChangeListener (this); }

void StepSequencer::timerCallback()
{
    const int now = playingStep (c, seq.stepTicks, (int) seq.steps.size());
    if (now != lit) { lit = now; repaint(); }
}

void StepSequencer::read()
{
    const auto* clip = c.project.clip (c.selectedClip);
    if (! clip || ! clip->isMidi()) { seq = {}; return; }
    const int count = jlimit (1, 64, (int) ((clip->loopLength > 0 ? clip->loopLength : clip->length) / jmax<Tick> (1, c.grid)));
    seq = patterns::readSteps (clip->midi, c.grid, count, clip->offset, NoteFrom::Steps);
    for (auto& s : seq.steps) if (! s.on && s.pitch == 60) s.pitch = 60;
}

void StepSequencer::write (const String& label)
{
    const Id id = c.selectedClip;
    auto s = seq;
    auto doWrite = [id, s] (Project& p)
    {
        auto* clip = p.clip (id);
        if (! clip) return;
        unsigned next = p.nextId;
        patterns::writeSteps (clip->midi, s, clip->offset, clip->loopLength > 0 ? clip->loopLength : clip->length, next, NoteFrom::Steps);
        p.nextId = std::max (p.nextId, (Id) next);
    };
    if (label.isEmpty()) { doWrite (c.project); c.refresh(); }
    else c.apply (label, doWrite);
}

int StepSequencer::stepAt (int x) const
{
    const int n = (int) seq.steps.size();
    if (n == 0) return -1;
    const auto area = getLocalBounds().reduced (theme::px (12), theme::px (12));
    const int s = (int) ((float) (x - area.getX()) / ((float) area.getWidth() / (float) n));
    return s >= 0 && s < n ? s : -1;
}

void StepSequencer::paint (Graphics& g)
{
    g.fillAll (theme::roll);
    const auto* clip = c.project.clip (c.selectedClip);
    if (! clip || ! clip->isMidi()) { emptyMessage (g, getLocalBounds(), "Choose a MIDI clip to sequence."); return; }
    const auto area = getLocalBounds().reduced (theme::px (12), theme::px (12));
    const int n = (int) seq.steps.size();
    const float w = (float) area.getWidth() / (float) n;
    const auto& scale = c.project.scale;
    const int playing = playingStep (c, seq.stepTicks, n);
    for (int i = 0; i < n; ++i)
    {
        const auto& s = seq.steps[(size_t) i];
        auto cell = Rectangle<float> ((float) area.getX() + (float) i * w, (float) area.getY(), w, (float) area.getHeight()).reduced (2.0f);
        g.setColour (theme::sunken);
        g.fillRoundedRectangle (cell, 4.0f);
        if (i == playing)
        {
            lightColumn (g, cell);
            g.setColour (theme::playhead);
            g.drawRoundedRectangle (cell, 4.0f, 1.5f);
        }
        if (s.on)
        {
            // Height is pitch; the bar inside is velocity.
            const float h = cell.getHeight() * (0.15f + 0.85f * (float) (s.pitch - 24) / 84.0f);
            auto bar = cell.withTop (cell.getBottom() - jlimit (8.0f, cell.getHeight(), h));
            g.setColour (theme::accent);
            g.fillRoundedRectangle (bar, 4.0f);
            g.setColour (theme::ink);
            g.fillRect (bar.getX() + 3, bar.getBottom() - 4 - (bar.getHeight() - 8) * (float) s.velocity / 127.0f, 3.0f, (bar.getHeight() - 8) * (float) s.velocity / 127.0f);
            g.setFont (theme::font (theme::smallPoints));
            g.drawText (String (harmony::noteName (scale, s.pitch)), bar.removeFromTop (16).toNearestInt(), Justification::centred, false);
            if (s.tie)
            {
                g.setColour (theme::accent);
                g.fillRect (cell.getRight() - 1, cell.getBottom() - 10, 6.0f, 4.0f);
            }
        }
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        g.drawText (String (i + 1), cell.toNearestInt().removeFromTop (14), Justification::centred, false);
    }
}

void StepSequencer::mouseDown (const MouseEvent& e)
{
    const int s = stepAt (e.x);
    if (s < 0) return;
    before = c.project;
    changed = false;
    dragStep = s;
    auto& step = seq.steps[(size_t) s];
    if (e.mods.isPopupMenu()) { step.on = false; write ("Step"); return; }
    if (! step.on)
    {
        step.on = true;
        // A new step starts on the note before it, so a line can be drawn quickly.
        for (int k = s - 1; k >= 0; --k) if (seq.steps[(size_t) k].on) { step.pitch = seq.steps[(size_t) k].pitch; break; }
        changed = true;
        c.engine.audition (c.focusTrack, step.pitch, step.velocity, 0.2);
        write ({});
    }
    dragPitch = step.pitch;
    dragVelocity = step.velocity;
}

void StepSequencer::mouseDrag (const MouseEvent& e)
{
    if (dragStep < 0) return;
    auto& step = seq.steps[(size_t) dragStep];
    const int d = -e.getDistanceFromDragStartY() / 6;
    if (e.mods.isAltDown()) step.velocity = jlimit (1, 127, dragVelocity - e.getDistanceFromDragStartY());
    else
    {
        int p = jlimit (0, 127, dragPitch + d);
        // In the scale, when it is shaded.
        if (c.project.scale.highlight)
        {
            int steps = 0, q = dragPitch;
            const int dir = d > 0 ? 1 : -1;
            while (steps < std::abs (d) && q > 0 && q < 127)
            {
                q += dir;
                if (harmony::inScale (c.project.scale, q)) ++steps;
            }
            p = q;
        }
        if (p != step.pitch) { step.pitch = p; c.engine.audition (c.focusTrack, p, step.velocity, 0.12); }
    }
    changed = true;
    write ({});
    repaint();
}

void StepSequencer::mouseUp (const MouseEvent& e)
{
    if (dragStep >= 0 && ! changed && e.getDistanceFromDragStart() < 3)
    {
        seq.steps[(size_t) dragStep].on = false;
        write ({});
    }
    if (! (before == c.project)) c.history.push (before, "Steps");
    dragStep = -1;
    c.refresh();
}

void StepSequencer::mouseDoubleClick (const MouseEvent& e)
{
    const int s = stepAt (e.x);
    if (s < 0) return;
    seq.steps[(size_t) s].on = true;
    seq.steps[(size_t) s].tie = ! seq.steps[(size_t) s].tie;
    write ("Tie");
}

void StepSequencer::changeListenerCallback (ChangeBroadcaster*)
{
    if (dragStep < 0) read();
    repaint();
}

//==============================================================================
EuclidEditor::EuclidEditor (Controller& ctrl) : c (ctrl)
{
    setTitle ("Euclidean sequencer");
    addAndMakeVisible (addRow);
    addRow.onClick = [this]
    {
        auto r = std::make_unique<Row>();
        const int pitches[] { 36, 38, 42, 39, 46, 37, 45, 50 };
        r->pitch = pitches[rows.size() % 8];
        rows.push_back (std::move (r));
        build();
        if (auto* p2 = getParentComponent()) setSize (getWidth(), jmax (p2->getHeight(), preferredHeight()));
    };
    // Kick, snare and hat to start.
    for (auto [p, steps, pulses, rot] : { std::tuple { 36, 16, 4, 0 }, { 38, 16, 2, 4 }, { 42, 16, 7, 0 } })
    {
        auto r = std::make_unique<Row>();
        r->pitch = p; r->steps = steps; r->pulses = pulses; r->rotation = rot;
        rows.push_back (std::move (r));
    }
    build();
    startTimerHz (30);
}

EuclidEditor::~EuclidEditor() = default;

std::vector<int> EuclidEditor::litSteps() const
{
    std::vector<int> v;
    for (auto& r : rows) v.push_back (playingStep (c, c.grid, r->steps));
    return v;
}

void EuclidEditor::timerCallback()
{
    auto now = litSteps();
    if (now != lit) { lit = std::move (now); repaint(); }
}

void EuclidEditor::build()
{
    const auto kit = patterns::standardKit (1);
    for (auto& rp : rows)
    {
        auto& r = *rp;
        for (auto& k : kit) if (k.pitch == r.pitch) r.name = k.name;
        if (r.name.isEmpty()) r.name = "Note " + String (r.pitch);
        if (! r.stepsKnob)
        {
            auto make = [this, &r] (int lo, int hi, int value, const String& name)
            {
                auto s = std::make_unique<Slider> (Slider::RotaryHorizontalVerticalDrag, Slider::TextBoxBelow);
                s->setRange (lo, hi, 1);
                s->setValue (value, dontSendNotification);
                s->setTitle (name);
                s->setTooltip (name);
                s->setTextBoxStyle (Slider::TextBoxBelow, false, theme::px (40), theme::px (16));
                s->onDragEnd = [this, &r] { apply (r); };
                s->onValueChange = [this, &r] { r.steps = (int) r.stepsKnob->getValue(); r.pulses = jmin ((int) r.pulsesKnob->getValue(), r.steps); r.rotation = (int) r.rotationKnob->getValue(); repaint(); };
                addAndMakeVisible (*s);
                return s;
            };
            r.stepsKnob = make (1, 32, r.steps, "Steps");
            r.pulsesKnob = make (0, 32, r.pulses, "Hits");
            r.rotationKnob = make (0, 31, r.rotation, "Rotate");
            r.drum = std::make_unique<theme::HouseButton> (r.name, "Choose the drum this rhythm plays.");
            r.drum->onClick = [this, &r]
            {
                PopupMenu m;
                for (auto& k : patterns::standardKit (1))
                    m.addItem (k.name, true, k.pitch == r.pitch, [this, &r, k] { r.pitch = k.pitch; r.name = k.name; r.drum->setButtonText (k.name); apply (r); });
                m.showMenuAsync (PopupMenu::Options().withTargetComponent (r.drum.get()));
            };
            addAndMakeVisible (*r.drum);
        }
    }
    resized();
    repaint();
}

void EuclidEditor::apply (Row& r)
{
    const Id id = c.selectedClip;
    if (! c.project.clip (id)) { c.say ("Choose a MIDI clip for the rhythm to go into."); return; }
    const Tick step = c.grid;
    patterns::Grid g;
    g.stepTicks = step;
    g.steps = r.steps;
    patterns::Lane lane { r.name.toStdString(), r.pitch, {} };
    patterns::euclidLane (lane, r.steps, r.pulses, r.rotation, r.velocity, r.pulses > 3 ? r.pulses : 0);
    g.lanes = { lane };
    c.apply ("Euclidean Rhythm", [id, g] (Project& p)
    {
        auto* clip = p.clip (id);
        if (! clip) return;
        unsigned next = p.nextId;
        patterns::writeGrid (clip->midi, g, clip->offset, clip->loopLength > 0 ? clip->loopLength : clip->length, next, NoteFrom::Euclid);
        p.nextId = std::max (p.nextId, (Id) next);
    });
}

void EuclidEditor::resized()
{
    auto area = getLocalBounds().reduced (theme::px (12));
    auto top = area.removeFromTop (theme::px (28));
    addRow.setBounds (top.removeFromRight (addRow.idealWidth()));
    area.removeFromTop (theme::px (8));
    const int rowH = theme::px (90);
    for (auto& rp : rows)
    {
        auto r = area.removeFromTop (rowH);
        rp->drum->setBounds (r.removeFromLeft (theme::px (110)).withSizeKeepingCentre (theme::px (110), theme::px (28)));
        r.removeFromLeft (theme::px (8));
        r.removeFromLeft (r.getHeight() + theme::px (8));   // the circle
        for (auto* k : { rp->stepsKnob.get(), rp->pulsesKnob.get(), rp->rotationKnob.get() })
            k->setBounds (r.removeFromLeft (theme::px (64)));
    }
}

void EuclidEditor::paint (Graphics& g)
{
    g.fillAll (theme::roll);
    auto area = getLocalBounds().reduced (theme::px (12));
    g.setColour (theme::textSecondary);
    g.setFont (theme::font (theme::smallPoints));
    g.drawText (c.project.clip (c.selectedClip) ? "Turn a dial and let go: the rhythm is written into the chosen clip, one step per grid line."
                                                : "Choose a MIDI clip for the rhythms to go into.",
                area.removeFromTop (theme::px (28)), Justification::centredLeft, true);
    area.removeFromTop (theme::px (8));
    const int rowH = theme::px (90);
    const auto playing = litSteps();
    size_t row = 0;
    for (auto& rp : rows)
    {
        const int now = row < playing.size() ? playing[row] : -1;
        ++row;
        auto r = area.removeFromTop (rowH);
        r.removeFromLeft (theme::px (118));
        auto circle = r.removeFromLeft (r.getHeight()).toFloat().reduced (6.0f);
        const auto hits = patterns::euclid (rp->steps, rp->pulses, rp->rotation);
        const auto centre = circle.getCentre();
        const float radius = circle.getWidth() * 0.5f;
        g.setColour (theme::rule);
        g.drawEllipse (circle, 1.0f);
        Path poly;
        bool first = true;
        for (int i = 0; i < (int) hits.size(); ++i)
        {
            const float a = MathConstants<float>::twoPi * (float) i / (float) hits.size() - MathConstants<float>::halfPi;
            const Point<float> p (centre.x + radius * std::cos (a), centre.y + radius * std::sin (a));
            g.setColour (hits[(size_t) i] ? theme::accent : theme::edge);
            const float d = hits[(size_t) i] ? 7.0f : 4.0f;
            g.fillEllipse (p.x - d / 2, p.y - d / 2, d, d);
            if (i == now)
            {
                // The step being played: ringed, with a hand from the middle.
                g.setColour (theme::playhead);
                g.drawEllipse (p.x - 7.0f, p.y - 7.0f, 14.0f, 14.0f, 1.5f);
                g.drawLine (Line<float> (centre, p), 1.0f);
            }
            if (hits[(size_t) i]) { if (first) { poly.startNewSubPath (p); first = false; } else poly.lineTo (p); }
        }
        poly.closeSubPath();
        g.setColour (theme::accent.withAlpha (0.4f));
        g.strokePath (poly, PathStrokeType (1.0f));
    }
}

} // namespace kal
