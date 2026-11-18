#include "PatternEditors.h"

#include "Harmony.h"
#include "Ideas.h"

namespace kal
{
using namespace juce;

// The menu of what to set, on the button that shows it (decisions 0061, 0063).
static void optionMenu (theme::HouseButton& b, noteopt::Kind now, std::function<void (noteopt::Kind)> pick)
{
    PopupMenu m;
    noteopt::addTo (m, now, [&b, pick] (noteopt::Kind k)
    {
        b.setButtonText (noteopt::name (k) + String::fromUTF8 (" \xe2\x96\xbe"));
        b.setTip (noteopt::tip (k));
        b.setSize (b.idealWidth (theme::smallControl), b.getHeight());   // the new name, whole
        if (auto* parent = b.getParentComponent()) parent->resized();     // what's beside it moves along
        pick (k);
    }, true);
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&b));
}

//==============================================================================
FeelControls::FeelControls()
{
    auto setUp = [this] (Slider& s, double lo, double hi, const String& name, const String& tip)
    {
        s.setSliderStyle (Slider::LinearHorizontal);
        s.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        s.setRange (lo, hi, 1.0);
        s.setTitle (name);
        s.setTooltip (tip);
        s.onDragStart = [this] { if (began) began(); };
        s.onValueChange = [this] { repaint(); if (changed) changed (swing.getValue() / 100.0, gate.getValue() / 100.0); };
        s.onDragEnd = [this] { if (ended) ended(); };
        addAndMakeVisible (s);
    };
    setUp (swing, 0, 75, "Swing", "Swing: every second step played late, by up to three quarters of a step.");
    setUp (gate, 5, 100, "Gate", "Gate: how long each note sounds, as a share of its step.");
}

void FeelControls::set (const PatternFeel& f)
{
    swing.setValue (std::round (f.swing * 100.0), dontSendNotification);
    gate.setValue (std::round (f.gate * 100.0), dontSendNotification);
    repaint();
}

int FeelControls::idealWidth() const { return 2 * (labelWidth() + sliderWidth() + valueWidth()) + theme::px (12); }

void FeelControls::resized()
{
    auto r = getLocalBounds();
    for (auto* s : { &swing, &gate })
    {
        r.removeFromLeft (labelWidth());
        s->setBounds (r.removeFromLeft (sliderWidth()));
        r.removeFromLeft (valueWidth() + theme::px (12));
    }
}

void FeelControls::paint (Graphics& g)
{
    g.setFont (theme::font (theme::smallPoints));
    for (auto* s : { &swing, &gate })
    {
        g.setColour (theme::textSecondary);
        g.drawText (s->getTitle(), Rectangle<int> (s->getX() - labelWidth(), 0, labelWidth() - theme::px (4), getHeight()), Justification::centredRight, false);
        g.setColour (theme::text);
        g.drawText (String (roundToInt (s->getValue())) + "%", Rectangle<int> (s->getRight() + theme::px (4), 0, valueWidth(), getHeight()), Justification::centredLeft, false);
    }
}

static double stepValue (const patterns::Step& s, noteopt::Kind k)
{
    return k == noteopt::Kind::Chance ? s.chance : k == noteopt::Kind::VelocityRange ? s.velocityRange
         : k == noteopt::Kind::Timing ? s.timingMs : k == noteopt::Kind::Repeat ? s.repeat : s.velocity;
}

static void setStepValue (patterns::Step& s, noteopt::Kind k, double v)
{
    const int i = jlimit (noteopt::lowest (k), noteopt::highest (k), roundToInt (v));
    if (k == noteopt::Kind::Chance) s.chance = i;
    else if (k == noteopt::Kind::VelocityRange) s.velocityRange = i;
    else if (k == noteopt::Kind::Timing) s.timingMs = i;
    else if (k == noteopt::Kind::Repeat) s.repeat = i;
    else s.velocity = i;
}

// A drag of `dy` pixels up, in the option's own units: its whole range over 127 pixels.
static double perPixel (noteopt::Kind k) { return (noteopt::highest (k) - noteopt::lowest (k)) / 127.0; }

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
    // From the start of what the editors show: the clip's window (a loop from 0).
    const auto w = patterns::windowOf (*clip);
    Tick rel = at - clip->start + clip->offset - w.from;
    if (clip->loopLength > 0) rel = (at - clip->start + clip->offset) % clip->loopLength;
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
    // Swing and gate: a drag is one undo step (decision 0063).
    feel.began = [this] { before = c.project; };
    feel.changed = [this] (double sw, double gt) { grid.swing = sw; grid.gate = gt; if (c.project.clip (c.selectedClip)) write ({}); };
    feel.ended = [this] { if (! (before == c.project)) c.history.push (before, "Swing and Gate"); c.refresh(); };
    addAndMakeVisible (option);
    option.onClick = [this] { optionMenu (option, kind, [this] (noteopt::Kind k) { kind = k; repaint(); }); };
    addAndMakeVisible (randomise);
    randomise.onClick = [this] { random(); };
    addAndMakeVisible (feel);
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
    return jlimit (1, 64, (int) (patterns::windowOf (clip).length / jmax<Tick> (1, c.grid)));
}

void DrumGrid::read()
{
    const auto* clip = c.project.clip (c.selectedClip);
    grid = {};
    if (! clip || ! clip->isMidi()) return;
    grid.stepTicks = c.grid;
    grid.steps = stepsFor (*clip);
    grid.swing = clip->drumsFeel.swing;
    grid.gate = clip->drumsFeel.gate;
    grid.lanes = patterns::standardKit (grid.steps);
    grid = patterns::readGrid (clip->midi, grid, patterns::windowOf (*clip).from, NoteFrom::Drums);
    feel.set (clip->drumsFeel);
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
        clip->drumsFeel = { g.swing, g.gate };
        patterns::writeGrid (clip->midi, g, patterns::windowOf (*clip).from, patterns::windowOf (*clip).length, next, NoteFrom::Drums);
        p.nextId = std::max (p.nextId, (Id) next);
    };
    if (label.isEmpty()) { doWrite (c.project); c.refresh(); }
    else c.apply (label, doWrite);
}

Rectangle<int> DrumGrid::cellArea() const { return getLocalBounds().withTrimmedTop (topHeight()).withTrimmedLeft (nameWidth()).reduced (0, 4); }

void DrumGrid::resized()
{
    option.setBounds (theme::px (6), theme::px (4), option.idealWidth (theme::smallControl), theme::px (26));
    randomise.setBounds (option.getRight() + theme::px (6), theme::px (4), randomise.idealWidth (theme::smallControl), theme::px (26));
    feel.setBounds (randomise.getRight() + theme::px (18), theme::px (4), feel.idealWidth(), theme::px (26));
}

// A new idea number each click (decision 0062): Good Idea's numbers run 1 to 99,999.
static int newSeed() { return Random::getSystemRandom().nextInt (ideas::maxSeed) + 1; }

static String said (const ideas::Idea& idea)
{
    String s = "Random: " + String (idea.name);
    if (! idea.summary.empty()) s << " (" << String (idea.summary).replace ("  /  ", ", ") << ")";
    return s + ". Click Random again for another.";
}

// Good Idea's drums for as many bars as the grid shows, snapped onto its steps.
void DrumGrid::random()
{
    const auto* clip = c.project.clip (c.selectedClip);
    if (! clip || ! clip->isMidi()) { c.say ("Choose a MIDI clip to draw drums into."); return; }
    const auto& meter = c.project.tempo.meterAt (clip->start);
    const int bars = jlimit (1, 16, (int) (grid.stepTicks * grid.steps / jmax<Tick> (1, meter.barTicks())));
    const auto idea = ideas::drums (bars, meter.numerator, meter.denominator, newSeed());
    if (! idea.error.empty()) { c.say ("Good Idea's engine failed: " + String (idea.error), true); return; }
    MidiData made;
    for (auto n : idea.notes) { n.from = NoteFrom::Drums; made.notes.push_back (n); }
    auto layout = grid;
    layout.lanes = patterns::standardKit (grid.steps);
    grid = patterns::readGrid (made, layout, 0, NoteFrom::Drums);
    write ("Random Drums");
    c.say (said (idea), false, true);
    if (auto* parent = getParentComponent()) setSize (getWidth(), jmax (parent->getHeight(), preferredHeight()));
    repaint();
}

double DrumGrid::valueAt (int l, int s) const
{
    const auto& lane = grid.lanes[(size_t) l];
    const auto i = (size_t) s;
    switch (kind)
    {
        case noteopt::Kind::Chance:        return i < lane.chance.size() ? lane.chance[i] : 100;
        case noteopt::Kind::VelocityRange: return i < lane.velocityRange.size() ? lane.velocityRange[i] : 0;
        case noteopt::Kind::Timing:        return i < lane.timingMs.size() ? lane.timingMs[i] : 0;
        case noteopt::Kind::Repeat:        return i < lane.repeat.size() ? lane.repeat[i] : 1;
        default:                           return lane.velocity[i];
    }
}

void DrumGrid::setValueAt (int l, int s, double v)
{
    auto& lane = grid.lanes[(size_t) l];
    const auto i = (size_t) s;
    const int n = jlimit (noteopt::lowest (kind), noteopt::highest (kind), roundToInt (v));
    const auto steps = lane.velocity.size();
    if (lane.chance.size() < steps) lane.chance.resize (steps, 100);
    if (lane.velocityRange.size() < steps) lane.velocityRange.resize (steps, 0);
    if (lane.timingMs.size() < steps) lane.timingMs.resize (steps, 0.0);
    if (lane.repeat.size() < steps) lane.repeat.resize (steps, 1);
    switch (kind)
    {
        case noteopt::Kind::Chance:        lane.chance[i] = n; break;
        case noteopt::Kind::VelocityRange: lane.velocityRange[i] = n; break;
        case noteopt::Kind::Timing:        lane.timingMs[i] = n; break;
        case noteopt::Kind::Repeat:        lane.repeat[i] = n; break;
        default:                           lane.velocity[i] = n; break;
    }
}

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
                // The chosen option's value, as a bar (velocity, as a shape too), and in figures where there's room.
                const double value = valueAt (l, s);
                g.setColour (theme::ink);
                const float h = (cell.getHeight() - 4) * (float) (value / noteopt::highest (kind));
                g.fillRect (cell.getX() + 2, cell.getBottom() - 2 - h, 2.0f, h);
                // Repeats: the cell cut into as many (decision 0063).
                const int times = (size_t) s < lane.repeat.size() ? lane.repeat[(size_t) s] : 1;
                for (int k = 1; k < times; ++k)
                    g.fillRect (cell.getX() + cell.getWidth() * (float) k / (float) times - 0.5f, cell.getY() + 4.0f, 1.0f, cell.getHeight() - 8.0f);
                if (kind != noteopt::Kind::Velocity && noteopt::notPlain (kind, value) && colW >= (float) theme::px (34))
                {
                    g.setFont (theme::font (theme::smallPoints));
                    g.drawText (noteopt::format (kind, value), cell.withTrimmedLeft (5).toNearestInt(), Justification::centred, false);
                }
            }
            if (s == playing)
            {
                g.setColour (theme::playhead);
                g.drawRoundedRectangle (cell, 3.0f, 1.5f);
            }
        }
    }
    // Chosen hits (Select, decision 0075): ringed.
    if (! c.selectedNotes.empty())
    {
        const auto w = patterns::windowOf (*clip);
        g.setColour (theme::text);
        for (auto& n : clip->midi.notes)
        {
            if (n.from != NoteFrom::Drums || ! c.selectedNotes.count (n.id) || grid.stepTicks <= 0) continue;
            const int s = (int) (((n.start - w.from) / grid.stepTicks) % jmax (1, grid.steps));
            for (int l = 0; l < lanes; ++l)
                if (grid.lanes[(size_t) l].pitch == n.pitch)
                    g.drawRoundedRectangle (Rectangle<float> ((float) area.getX() + (float) s * colW, (float) area.getY() + (float) l * rowH, colW, rowH).reduced (0.5f), 3.0f, 2.0f);
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
    else if (v > 0 && ! e.mods.isShiftDown()) { dragLane = l; dragStep = s; dragStartValue = valueAt (l, s); return; }
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
    setValueAt (dragLane, dragStep, dragStartValue - e.getDistanceFromDragStartY() * perPixel (kind));
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
    // Swing, and a gate every step takes: a drag is one undo step (decision 0063).
    feel.began = [this] { before = c.project; };
    feel.changed = [this] (double sw, double gt)
    {
        seq.swing = sw;
        gateSetting = gt;
        for (auto& st : seq.steps) st.gate = gt;
        if (c.project.clip (c.selectedClip)) write ({});
    };
    feel.ended = [this] { if (! (before == c.project)) c.history.push (before, "Swing and Gate"); c.refresh(); };
    addAndMakeVisible (option);
    option.onClick = [this] { optionMenu (option, kind, [this] (noteopt::Kind k) { kind = k; repaint(); }); };
    addAndMakeVisible (randomise);
    randomise.onClick = [this] { random(); };
    addAndMakeVisible (feel);
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
    const int count = jlimit (1, 64, (int) (patterns::windowOf (*clip).length / jmax<Tick> (1, c.grid)));
    seq = patterns::readSteps (clip->midi, c.grid, count, patterns::windowOf (*clip).from, NoteFrom::Steps);
    seq.swing = clip->stepsFeel.swing;
    gateSetting = clip->stepsFeel.gate;
    for (auto& s : seq.steps) if (! s.on) s.gate = gateSetting;
    feel.set (clip->stepsFeel);
}

void StepSequencer::write (const String& label)
{
    const Id id = c.selectedClip;
    auto s = seq;
    const double gt = gateSetting;
    auto doWrite = [id, s, gt] (Project& p)
    {
        auto* clip = p.clip (id);
        if (! clip) return;
        unsigned next = p.nextId;
        clip->stepsFeel = { s.swing, gt };
        patterns::writeSteps (clip->midi, s, patterns::windowOf (*clip).from, patterns::windowOf (*clip).length, next, NoteFrom::Steps);
        p.nextId = std::max (p.nextId, (Id) next);
    };
    if (label.isEmpty()) { doWrite (c.project); c.refresh(); }
    else c.apply (label, doWrite);
}

Rectangle<int> StepSequencer::cellsArea() const
{
    return getLocalBounds().reduced (theme::px (12), theme::px (12)).withTrimmedTop (theme::px (30));
}

void StepSequencer::resized()
{
    option.setBounds (theme::px (12), theme::px (8), option.idealWidth (theme::smallControl), theme::px (26));
    randomise.setBounds (option.getRight() + theme::px (6), theme::px (8), randomise.idealWidth (theme::smallControl), theme::px (26));
    feel.setBounds (randomise.getRight() + theme::px (18), theme::px (8), feel.idealWidth(), theme::px (26));
}

// Good Idea's motif in the song's key, one note a step (the top one where it plays two).
void StepSequencer::random()
{
    const auto* clip = c.project.clip (c.selectedClip);
    if (! clip || ! clip->isMidi() || seq.steps.empty()) { c.say ("Choose a MIDI clip to sequence."); return; }
    const auto& meter = c.project.tempo.meterAt (clip->start);
    const int bars = jlimit (1, 4, (int) (seq.stepTicks * (Tick) seq.steps.size() / jmax<Tick> (1, meter.barTicks())));
    const auto idea = ideas::motif (c.project.scale, bars, meter.numerator, meter.denominator, newSeed());
    if (! idea.error.empty()) { c.say ("Good Idea's engine failed: " + String (idea.error), true); return; }
    MidiData made;
    for (auto n : idea.notes) { n.from = NoteFrom::Steps; made.notes.push_back (n); }
    const auto stepTicks = seq.stepTicks;
    seq = patterns::readSteps (made, stepTicks, (int) seq.steps.size(), 0, NoteFrom::Steps);
    write ("Random Steps");
    c.say (said (idea), false, true);
    repaint();
}

int StepSequencer::stepAt (int x) const
{
    const int n = (int) seq.steps.size();
    if (n == 0) return -1;
    const auto area = cellsArea();
    const int s = (int) ((float) (x - area.getX()) / ((float) area.getWidth() / (float) n));
    return s >= 0 && s < n ? s : -1;
}

void StepSequencer::paint (Graphics& g)
{
    g.fillAll (theme::roll);
    const auto* clip = c.project.clip (c.selectedClip);
    if (! clip || ! clip->isMidi()) { emptyMessage (g, getLocalBounds(), "Choose a MIDI clip to sequence."); return; }
    const auto area = cellsArea();
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
        // A chosen step (Select, decision 0075): ringed.
        if (! c.selectedNotes.empty() && seq.stepTicks > 0)
        {
            const auto win = patterns::windowOf (*clip);
            for (auto& n : clip->midi.notes)
                if (n.from == NoteFrom::Steps && c.selectedNotes.count (n.id)
                    && (int) ((n.start - win.from) / seq.stepTicks) % jmax (1, (int) seq.steps.size()) == i)
                {
                    g.setColour (theme::text);
                    g.drawRoundedRectangle (cell.expanded (1.0f), 4.0f, 2.0f);
                    break;
                }
        }
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
            // The chosen option's value, as a bar inside (velocity unless another is chosen), and in figures.
            const float share = (float) (stepValue (s, kind) / noteopt::highest (kind));
            g.setColour (theme::ink);
            g.fillRect (bar.getX() + 3, bar.getBottom() - 4 - (bar.getHeight() - 8) * share, 3.0f, (bar.getHeight() - 8) * share);
            // Repeats: the bar cut into as many (decision 0063).
            for (int k = 1; k < s.repeat; ++k)
                g.fillRect (bar.getX() + bar.getWidth() * (float) k / (float) s.repeat - 0.5f, bar.getY() + 18.0f, 1.0f, jmax (0.0f, bar.getHeight() - 22.0f));
            if (kind != noteopt::Kind::Velocity)
            {
                g.setFont (theme::font (theme::smallPoints));
                g.drawText (noteopt::format (kind, stepValue (s, kind)), bar.withTrimmedBottom (4).toNearestInt().removeFromBottom (16), Justification::centred, false);
            }
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
        step.gate = gateSetting;
        // A new step starts on the note before it, so a line can be drawn quickly.
        for (int k = s - 1; k >= 0; --k) if (seq.steps[(size_t) k].on) { step.pitch = seq.steps[(size_t) k].pitch; break; }
        changed = true;
        c.engine.audition (c.focusTrack, step.pitch, step.velocity, 0.2);
        write ({});
    }
    dragPitch = step.pitch;
    dragVelocity = step.velocity;
    dragValue = stepValue (step, kind);
}

void StepSequencer::mouseDrag (const MouseEvent& e)
{
    if (dragStep < 0) return;
    auto& step = seq.steps[(size_t) dragStep];
    const int d = -e.getDistanceFromDragStartY() / 6;
    if (e.mods.isAltDown() || kind != noteopt::Kind::Velocity)
        setStepValue (step, kind, dragValue - e.getDistanceFromDragStartY() * perPixel (kind));
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
    c.addChangeListener (this);
    // Swing and gate for every drum's rhythm: a drag is one undo step (decision 0063).
    feel.began = [this] { before = c.project; };
    feel.changed = [this] (double sw, double gt)
    {
        if (auto* clip = c.project.clip (c.selectedClip)) { clip->euclidFeel = { sw, gt }; applyAll ({}); }
    };
    feel.ended = [this] { if (! (before == c.project)) c.history.push (before, "Swing and Gate"); c.refresh(); };
    setTitle ("Euclidean sequencer");
    addAndMakeVisible (addRow);
    addAndMakeVisible (randomise);
    randomise.onClick = [this] { random(); };
    addAndMakeVisible (feel);
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

EuclidEditor::~EuclidEditor() { c.removeChangeListener (this); }

void EuclidEditor::changeListenerCallback (ChangeBroadcaster*)
{
    if (const auto* clip = c.project.clip (c.selectedClip)) feel.set (clip->euclidFeel);
}

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

// Every drum's rhythm rolled afresh (decision 0062), written as one undo step.
void EuclidEditor::random()
{
    if (! c.project.clip (c.selectedClip)) { c.say ("Choose a MIDI clip for the rhythms to go into."); return; }
    const unsigned seed = (unsigned) newSeed();
    for (auto& rp : rows)
    {
        const auto e = patterns::randomEuclid (rp->pitch, seed);
        rp->steps = e.steps;
        rp->pulses = e.pulses;
        rp->rotation = e.rotation;
        rp->stepsKnob->setValue (e.steps, dontSendNotification);
        rp->pulsesKnob->setValue (e.pulses, dontSendNotification);
        rp->rotationKnob->setValue (e.rotation, dontSendNotification);
    }
    applyAll ("Random Rhythms");
    c.say ("Random: new rhythms for every drum. Click Random again for others.", false, true);
    repaint();
}

// A row's rhythm as a one-row grid, with the clip's swing and gate (decision 0063).
patterns::Grid EuclidEditor::gridFor (const Row& r, const PatternFeel& f) const
{
    patterns::Grid one;
    one.stepTicks = c.grid;
    one.steps = r.steps;
    one.swing = f.swing;
    one.gate = f.gate;
    patterns::Lane lane { r.name.toStdString(), r.pitch, {}, {}, {}, {}, {} };
    patterns::euclidLane (lane, r.steps, r.pulses, r.rotation, r.velocity, r.pulses > 3 ? r.pulses : 0);
    one.lanes = { lane };
    return one;
}

void EuclidEditor::applyAll (const String& label)
{
    const Id id = c.selectedClip;
    const auto* clip = c.project.clip (id);
    if (! clip) return;
    const auto f = clip->euclidFeel;
    std::vector<patterns::Grid> grids;
    for (auto& rp : rows) grids.push_back (gridFor (*rp, f));
    auto doWrite = [id, grids, f] (Project& p)
    {
        auto* cl = p.clip (id);
        if (! cl) return;
        cl->euclidFeel = f;
        unsigned next = p.nextId;
        for (auto& one : grids)
            patterns::writeGrid (cl->midi, one, patterns::windowOf (*cl).from, patterns::windowOf (*cl).length, next, NoteFrom::Euclid);
        p.nextId = std::max (p.nextId, (Id) next);
    };
    if (label.isEmpty()) { doWrite (c.project); c.refresh(); }
    else c.apply (label.toStdString(), doWrite);
}

void EuclidEditor::apply (Row& r)
{
    const Id id = c.selectedClip;
    if (! c.project.clip (id)) { c.say ("Choose a MIDI clip for the rhythm to go into."); return; }
    const auto g = gridFor (r, c.project.clip (id)->euclidFeel);
    c.apply ("Euclidean Rhythm", [id, g] (Project& p)
    {
        auto* clip = p.clip (id);
        if (! clip) return;
        unsigned next = p.nextId;
        patterns::writeGrid (clip->midi, g, patterns::windowOf (*clip).from, patterns::windowOf (*clip).length, next, NoteFrom::Euclid);
        p.nextId = std::max (p.nextId, (Id) next);
    });
}

void EuclidEditor::resized()
{
    auto area = getLocalBounds().reduced (theme::px (12));
    auto top = area.removeFromTop (theme::px (28));
    addRow.setBounds (top.removeFromRight (addRow.idealWidth()));
    top.removeFromRight (theme::px (6));
    randomise.setBounds (top.removeFromRight (randomise.idealWidth()));
    top.removeFromRight (theme::px (18));
    feel.setBounds (top.removeFromRight (feel.idealWidth()));
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
                area.removeFromTop (theme::px (28)).withRight (feel.getX() - theme::px (8)), Justification::centredLeft, true);
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
