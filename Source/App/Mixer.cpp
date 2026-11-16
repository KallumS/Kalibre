#include "Mixer.h"

namespace kal
{
using namespace juce;

class ChannelStrip : public Component
{
public:
    ChannelStrip (Controller& ctrl, Id track) : c (ctrl), id (track)
    {
        fader.setSliderStyle (Slider::LinearVertical);
        fader.setTextBoxStyle (Slider::TextBoxBelow, false, theme::px (60), theme::px (16));
        fader.setRange (-60.0, 6.0, 0.1);
        fader.setSkewFactorFromMidPoint (-12.0);
        fader.setDoubleClickReturnValue (true, 0.0);
        fader.setTextValueSuffix (" dB");
        fader.setTitle ("Volume");
        pan.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        pan.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setTitle ("Pan");
        pan.setTooltip ("Pan. Double-click for the centre.");
        for (auto* s : { &fader, &pan })
        {
            addAndMakeVisible (s);
            // Moved while playing, it writes automation (Touch, Latch, Write, Trim).
            auto target = [s, this]
            {
                AutoTarget a;
                a.kind = s == &pan ? AutoTarget::Kind::Pan : AutoTarget::Kind::Volume;
                return a;
            };
            auto normal = [s, this] { return s == &pan ? (pan.getValue() + 1.0) / 2.0 : (fader.getValue() + 60.0) / 66.0; };
            s->onDragStart = [this, s, target, normal]
            {
                before = c.project;
                c.touchedMix (id, s == &pan ? AutoTarget::Kind::Pan : AutoTarget::Kind::Volume, s == &pan ? "Pan" : "Volume");
                c.writeAutomation (id, target(), normal(), 1);
            };
            s->onValueChange = [this, s, target, normal]
            {
                if (auto* t = c.project.track (id)) { t->volumeDb = fader.getValue(); t->pan = pan.getValue(); }
                if (auto* f = c.engine.fader (id)) { f->volumeDb = (float) fader.getValue(); f->pan = (float) pan.getValue(); }
                c.dirty = true;
                if (s->isMouseButtonDown()) c.writeAutomation (id, target(), normal(), 0);
            };
            s->onDragEnd = [this, target, normal]
            {
                if (! (before == c.project)) c.history.push (before, "Mix");
                c.writeAutomation (id, target(), normal(), 2);
                c.refresh();
            };
        }
        // Front to back, in a surround mix (decision 0040).
        depth.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        depth.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        depth.setRange (-1.0, 1.0, 0.01);
        depth.setDoubleClickReturnValue (true, 1.0);
        depth.setTitle ("Front to Back");
        depth.setTooltip ("Where the track is from front (right) to back (left) in the surround mix. Pan is left to right.");
        depth.onDragStart = [this] { before = c.project; };
        depth.onValueChange = [this] { if (auto* t = c.project.track (id)) t->frontBack = depth.getValue(); if (auto* f = c.engine.fader (id)) f->frontBack = (float) depth.getValue(); c.dirty = true; };
        depth.onDragEnd = [this] { if (! (before == c.project)) c.history.push (before, "Surround Position"); c.refresh(); };
        addChildComponent (depth);
        for (auto* b : { &mute, &solo, &phase }) addAndMakeVisible (b);
        mute.onClick = [this]
        {
            c.apply ("Mute", [this] (Project& p) { if (auto* t = p.track (id)) t->mute = ! t->mute; });
            c.touchedMix (id, AutoTarget::Kind::Mute, "Mute");
            AutoTarget a;
            a.kind = AutoTarget::Kind::Mute;
            const bool on = c.project.track (id) && c.project.track (id)->mute;
            c.writeAutomation (id, a, on ? 1.0 : 0.0, 1);
            c.writeAutomation (id, a, on ? 1.0 : 0.0, 2);
        };
        solo.onClick = [this] { c.apply ("Solo", [this] (Project& p) { if (auto* t = p.track (id)) t->solo = ! t->solo; }); };
        phase.onClick = [this] { c.apply ("Polarity", [this] (Project& p) { if (auto* t = p.track (id)) t->phaseInvert = ! t->phaseInvert; }); };
        mute.setTitle ("Mute");
        solo.setTitle ("Solo");
        phase.setTitle ("Invert Polarity");
        update();
    }

    void update()
    {
        const auto* t = c.project.track (id);
        if (! t) return;
        fader.setValue (t->volumeDb, dontSendNotification);
        pan.setValue (t->pan, dontSendNotification);
        depth.setValue (t->frontBack, dontSendNotification);
        depth.setVisible (c.project.surround != SurroundFormat::Stereo && t->kind != TrackKind::Master && ! t->isFolder());
        mute.setToggleState (t->mute, dontSendNotification);
        solo.setToggleState (t->solo, dontSendNotification);
        phase.setToggleState (t->phaseInvert, dontSendNotification);
        solo.setVisible (t->kind != TrackKind::Master);
        // Sends, as small knobs.
        sends.clear();
        for (auto& s : t->sends)
        {
            auto* k = sends.add (new Slider (Slider::RotaryHorizontalVerticalDrag, Slider::NoTextBox));
            k->setRange (-60.0, 6.0, 0.1);
            k->setValue (s.gainDb, dontSendNotification);
            const auto* target = c.project.track (s.target);
            k->setTitle ("Send to " + String (target ? target->name : "?"));
            k->setTooltip (k->getTitle());
            auto sendTarget = [target = s.target] { AutoTarget a; a.kind = AutoTarget::Kind::SendLevel; a.send = target; return a; };
            k->onDragStart = [this, k, sendTarget] { before = c.project; c.writeAutomation (id, sendTarget(), (k->getValue() + 60.0) / 66.0, 1); };
            k->onValueChange = [this, k, target = s.target, sendTarget]
            {
                if (auto* tr = c.project.track (id))
                    for (auto& x : tr->sends) if (x.target == target) x.gainDb = k->getValue();
                if (k->isMouseButtonDown()) c.writeAutomation (id, sendTarget(), (k->getValue() + 60.0) / 66.0, 0);
                c.refresh();
            };
            k->onDragEnd = [this, k, sendTarget] { if (! (before == c.project)) c.history.push (before, "Send"); c.writeAutomation (id, sendTarget(), (k->getValue() + 60.0) / 66.0, 2); };
            addAndMakeVisible (k);
        }
        resized();
        repaint();
    }

    void paint (Graphics& g) override
    {
        const auto* t = c.project.track (id);
        if (! t) return;
        const bool chosen = c.focusTrack == id;
        g.setColour (chosen ? theme::raised : theme::sunken);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
        g.setColour (Colour (t->colour));
        g.fillRect (getLocalBounds().removeFromTop (3).reduced (4, 0));
        g.setColour (theme::text);
        g.setFont (theme::font (theme::smallPoints, true));
        g.drawFittedText (String (t->name), getLocalBounds().removeFromTop (theme::px (22)).reduced (4, 2), Justification::centred, 1);
        // The meter, beside the fader.
        auto m = meterArea();
        g.setColour (theme::roll);
        g.fillRect (m);
        auto level = [] (float peak) { return jlimit (0.0f, 1.0f, (Decibels::gainToDecibels (peak, -60.0f) + 60.0f) / 66.0f); };
        for (int ch = 0; ch < 2; ++ch)
        {
            auto bar = m.withWidth (m.getWidth() / 2).translated (ch * m.getWidth() / 2, 0).reduced (1, 0);
            const float v = level (meter[ch]);
            const auto filled = bar.withTop (bar.getBottom() - roundToInt ((float) bar.getHeight() * v));
            const float db = Decibels::gainToDecibels (meter[ch], -60.0f);
            g.setColour (db > -0.1f ? theme::warn : db > -6.0f ? theme::meterAmber : theme::meterGreen);
            g.fillRect (filled);
            // RMS (decision 0038): the average level, a light line across the peak bar.
            const float rv = level (rms[ch]);
            if (rv > 0.0f)
            {
                g.setColour (theme::playhead);
                g.fillRect (bar.getX(), bar.getBottom() - roundToInt ((float) bar.getHeight() * rv), bar.getWidth(), 2);
            }
        }
        // The track's plug-ins' share of the processor (decision 0038).
        if (t->kind != TrackKind::Master || ! t->devices.empty())
        {
            g.setColour (theme::textSecondary);
            g.setFont (theme::font (theme::smallPoints));
            g.drawText ("CPU " + String (cpu * 100.0, cpu < 0.1 ? 1 : 0) + "%", getLocalBounds().removeFromBottom (theme::px (16)).reduced (4, 0),
                        Justification::centredLeft, false);
        }
        if (held > 1.0f)
        {
            // Clipping: red mark at the top of the meter (a shape too: it fills the top cell).
            g.setColour (theme::warn);
            g.fillRect (m.withHeight (4));
        }
    }

    Rectangle<int> meterArea() const
    {
        auto r = getLocalBounds().reduced (4).withTrimmedTop (theme::px (22) + theme::px (40) + theme::px (24) + (int) sends.size() * theme::px (28));
        r.removeFromBottom (theme::px (32));
        return r.removeFromRight (theme::px (10));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        r.removeFromTop (theme::px (22));
        auto knobs = r.removeFromTop (theme::px (40));
        if (depth.isVisible())
        {
            pan.setBounds (knobs.removeFromLeft (knobs.getWidth() / 2).withSizeKeepingCentre (theme::px (34), theme::px (34)));
            depth.setBounds (knobs.withSizeKeepingCentre (theme::px (34), theme::px (34)));
        }
        else pan.setBounds (knobs.withSizeKeepingCentre (theme::px (40), theme::px (40)));
        auto row = r.removeFromTop (theme::px (24));
        const int bw = (row.getWidth() - 4) / 3;
        mute.setBounds (row.removeFromLeft (bw).reduced (1));
        solo.setBounds (row.removeFromLeft (bw).reduced (1));
        phase.setBounds (row.removeFromLeft (bw).reduced (1));
        for (auto* s : sends) s->setBounds (r.removeFromTop (theme::px (28)).withSizeKeepingCentre (theme::px (28), theme::px (28)));
        r.removeFromRight (theme::px (12));
        r.removeFromBottom (theme::px (16));   // the CPU reading
        fader.setBounds (r);
    }

    void tick()
    {
        if (auto* f = c.engine.fader (id))
            for (int ch = 0; ch < 2; ++ch)
            {
                const float p = f->takePeak (ch);
                meter[ch] = jmax (p, meter[ch] * 0.85f);
                if (p > 1.0f) held = 2.0f;
            }
        held *= 0.98f;
        if (auto* f = c.engine.fader (id)) { rms[0] = f->rmsL.load(); rms[1] = f->rmsR.load(); }
        const double now = c.engine.trackCpu (id);
        if (std::abs (now - cpu) > 0.0005) { cpu = now; repaint(); }
        repaint (meterArea().expanded (2));
    }

    void mouseDown (const MouseEvent&) override { c.focusOn (id); }

    Controller& c;
    Id id;
    Slider fader, pan, depth;
    theme::HouseButton mute { "M", "Mute.", true }, solo { "S", "Solo.", true }, phase { ui::u8 ("\xc3\x98"), "Invert the polarity.", true };
    OwnedArray<Slider> sends;
    float meter[2] {}, rms[2] {};
    float held = 0;
    double cpu = 0;
    Project before;
};

//==============================================================================
LoudnessPanel::LoudnessPanel (Controller& ctrl) : c (ctrl)
{
    setTitle ("Loudness");
    addAndMakeVisible (reset);
    reset.onClick = [this]
    {
        if (auto* f = c.engine.fader (c.project.master.id)) f->resetLoudness = true;
        history.clear();
        heldPeak = -100;
    };
}

void LoudnessPanel::tick()
{
    if (auto* f = c.engine.fader (c.project.master.id))
    {
        const auto& meter = f->loudnessMeter;
        m = meter.momentary(); s = meter.shortTerm(); i = meter.integrated();
        lra = meter.range(); tp = meter.truePeakDb(); corr = meter.correlation();
        heldPeak = jmax (heldPeak, (float) tp);
        if (c.engine.isPlaying())
        {
            history.push_back ((float) s);
            if (history.size() > 600) history.erase (history.begin());   // a minute at ten a second
        }
    }
    repaint();
}

void LoudnessPanel::paint (Graphics& g)
{
    g.setColour (theme::sunken);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
    auto r = getLocalBounds().reduced (theme::px (10), theme::px (8));
    g.setColour (theme::text);
    g.setFont (theme::font (theme::bodyPoints, true));
    g.drawText ("Loudness", r.removeFromTop (theme::px (20)), Justification::centredLeft, false);
    r.removeFromTop (theme::px (4));
    auto value = [] (double v, int places = 1) { return v <= -99.0 ? ui::u8 ("\xe2\x80\x94") : String (v, places); };
    struct Line { const char* name; String text; const char* unit; };
    const Line lines[] {
        { "Momentary", value (m), "LUFS" }, { "Short-term", value (s), "LUFS" }, { "Integrated", value (i), "LUFS" },
        { "Range", value (lra), "LU" }, { "True peak", value (heldPeak), "dBTP" }, { "Correlation", String (corr, 2), "" },
    };
    for (auto& line : lines)
    {
        auto row = r.removeFromTop (theme::px (20));
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        g.drawText (line.name, row, Justification::centredLeft, false);
        g.setColour (String (line.name) == "True peak" && heldPeak > -1.0f ? theme::warnText : theme::text);
        g.setFont (theme::font (theme::bodyPoints, true));
        const bool none = line.text == ui::u8 ("\xe2\x80\x94");
        g.drawText (line.text + (String (line.unit).isEmpty() || none ? String() : " " + String (line.unit)), row, Justification::centredRight, false);
    }
    // Correlation: a bar from -1 to +1; left of centre is out of phase.
    r.removeFromTop (theme::px (4));
    auto bar = r.removeFromTop (theme::px (8)).toFloat();
    g.setColour (theme::roll);
    g.fillRoundedRectangle (bar, 3.0f);
    g.setColour (theme::rule);
    g.fillRect (bar.getCentreX(), bar.getY(), 1.0f, bar.getHeight());
    const float cx = bar.getX() + bar.getWidth() * (float) (corr + 1.0) * 0.5f;
    g.setColour (corr < 0 ? theme::warn : theme::control);
    g.fillRect (Rectangle<float> (jmin (cx, bar.getCentreX()), bar.getY() + 1, std::abs (cx - bar.getCentreX()), bar.getHeight() - 2));

    // The short-term history, against lines at -14 (streaming) and -23 (broadcast).
    r.removeFromTop (theme::px (8));
    auto graph = r.withTrimmedBottom (theme::px (36)).toFloat();
    if (graph.getHeight() < 30) return;
    g.setColour (theme::roll);
    g.fillRect (graph);
    auto yOf = [&] (float lufs) { return graph.getBottom() - (jlimit (-40.0f, 0.0f, lufs) + 40.0f) / 40.0f * graph.getHeight(); };
    g.setFont (theme::font (theme::smallPoints));
    for (float ref : { -14.0f, -23.0f })
    {
        g.setColour (theme::rule);
        g.fillRect (graph.getX(), yOf (ref), graph.getWidth(), 1.0f);
        g.setColour (theme::textSecondary);
        g.drawText (String ((int) ref), graph.withY (yOf (ref) - 13).withHeight (12).toNearestInt(), Justification::topRight, false);
    }
    if (history.size() > 1)
    {
        Path p;
        const float step = graph.getWidth() / 600.0f;
        float x = graph.getRight() - step * (float) history.size();
        bool started = false;
        for (float v : history)
        {
            if (v > -70.0f) { if (! started) { p.startNewSubPath (x, yOf (v)); started = true; } else p.lineTo (x, yOf (v)); }
            x += step;
        }
        g.setColour (theme::control);
        g.strokePath (p, PathStrokeType (1.5f));
    }
}

void LoudnessPanel::resized()
{
    reset.setBounds (getLocalBounds().reduced (theme::px (10), theme::px (8)).removeFromBottom (theme::px (28)).removeFromLeft (reset.idealWidth()));
}

//==============================================================================
Mixer::Mixer (Controller& ctrl) : c (ctrl), loudness (ctrl)
{
    addAndMakeVisible (loudness);
    setTitle ("Mixer");
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&strip, false);
    viewport.setScrollBarsShown (false, true);
    c.addChangeListener (this);
    rebuild();
    startTimerHz (24);
}

Mixer::~Mixer() { c.removeChangeListener (this); }

void Mixer::rebuild()
{
    strips.clear();
    shape.clear();
    for (auto& t : c.project.tracks)
    {
        if (t.hidden) continue;
        strip.addAndMakeVisible (strips.add (new ChannelStrip (c, t.id)));
        shape << (int64) t.id << (int) t.sends.size() << ",";
    }
    strip.addAndMakeVisible (strips.add (new ChannelStrip (c, c.project.master.id)));
    resized();
}

void Mixer::changeListenerCallback (ChangeBroadcaster*)
{
    String s;
    for (auto& t : c.project.tracks) if (! t.hidden) s << (int64) t.id << (int) t.sends.size() << ",";
    if (s != shape) rebuild();
    else for (auto* x : strips) x->update();
    repaint();
}

void Mixer::timerCallback()
{
    for (auto* s : strips) s->tick();
    loudness.tick();
}

void Mixer::paint (Graphics& g) { g.fillAll (theme::ground); }

void Mixer::resized()
{
    auto area = getLocalBounds().reduced (theme::px (8), theme::px (6));
    loudness.setBounds (area.removeFromRight (theme::px (220)));
    area.removeFromRight (theme::px (8));
    viewport.setBounds (area);
    const int h = jmax (theme::px (200), viewport.getHeight() - viewport.getScrollBarThickness());
    const int w = theme::px (84);
    int x = 0;
    for (auto* s : strips)
    {
        if (s == strips.getLast()) x += theme::px (12);   // the master stands apart
        s->setBounds (x, 0, w, h);
        x += w + theme::px (4);
    }
    strip.setSize (jmax (x, viewport.getWidth()), h);
}

} // namespace kal
