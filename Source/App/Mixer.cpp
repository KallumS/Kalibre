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
            s->onDragStart = [this, s]
            {
                before = c.project;
                c.touchedMix (id, s == &pan ? AutoTarget::Kind::Pan : AutoTarget::Kind::Volume, s == &pan ? "Pan" : "Volume");
            };
            s->onValueChange = [this]
            {
                if (auto* t = c.project.track (id)) { t->volumeDb = fader.getValue(); t->pan = pan.getValue(); }
                if (auto* f = c.engine.fader (id)) { f->volumeDb = (float) fader.getValue(); f->pan = (float) pan.getValue(); }
                c.dirty = true;
            };
            s->onDragEnd = [this] { if (! (before == c.project)) c.history.push (before, "Mix"); c.refresh(); };
        }
        for (auto* b : { &mute, &solo, &phase }) addAndMakeVisible (b);
        mute.onClick = [this]
        {
            c.apply ("Mute", [this] (Project& p) { if (auto* t = p.track (id)) t->mute = ! t->mute; });
            c.touchedMix (id, AutoTarget::Kind::Mute, "Mute");
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
            k->onDragStart = [this] { before = c.project; };
            k->onValueChange = [this, k, target = s.target]
            {
                if (auto* tr = c.project.track (id))
                    for (auto& x : tr->sends) if (x.target == target) x.gainDb = k->getValue();
                c.refresh();
            };
            k->onDragEnd = [this] { if (! (before == c.project)) c.history.push (before, "Send"); };
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
        r.removeFromBottom (theme::px (16));
        return r.removeFromRight (theme::px (10));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        r.removeFromTop (theme::px (22));
        pan.setBounds (r.removeFromTop (theme::px (40)).withSizeKeepingCentre (theme::px (40), theme::px (40)));
        auto row = r.removeFromTop (theme::px (24));
        const int bw = (row.getWidth() - 4) / 3;
        mute.setBounds (row.removeFromLeft (bw).reduced (1));
        solo.setBounds (row.removeFromLeft (bw).reduced (1));
        phase.setBounds (row.removeFromLeft (bw).reduced (1));
        for (auto* s : sends) s->setBounds (r.removeFromTop (theme::px (28)).withSizeKeepingCentre (theme::px (28), theme::px (28)));
        r.removeFromRight (theme::px (12));
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
        repaint (meterArea().expanded (2));
    }

    void mouseDown (const MouseEvent&) override { c.focusOn (id); }

    Controller& c;
    Id id;
    Slider fader, pan;
    theme::HouseButton mute { "M", "Mute.", true }, solo { "S", "Solo.", true }, phase { ui::u8 ("\xc3\x98"), "Invert the polarity.", true };
    OwnedArray<Slider> sends;
    float meter[2] {};
    float held = 0;
    Project before;
};

//==============================================================================
Mixer::Mixer (Controller& ctrl) : c (ctrl)
{
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

void Mixer::timerCallback() { for (auto* s : strips) s->tick(); }

void Mixer::paint (Graphics& g) { g.fillAll (theme::ground); }

void Mixer::resized()
{
    viewport.setBounds (getLocalBounds().reduced (theme::px (8), theme::px (6)));
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
