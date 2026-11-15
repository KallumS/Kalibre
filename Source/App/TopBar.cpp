#include "TopBar.h"

#include "Harmony.h"
#include "ScaleModel.h"

namespace kal
{
using namespace juce;

void TopBar::Readout::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (theme::sunken);
    g.fillRoundedRectangle (r, 5.0f);
    if (isMouseOver()) { g.setColour (theme::edge); g.drawRoundedRectangle (r, 5.0f, 1.0f); }
    auto area = getLocalBounds().reduced (8, 2);
    g.setColour (theme::textSecondary);
    g.setFont (theme::font (theme::smallPoints));
    g.drawText (label, area.removeFromTop (area.getHeight() * 2 / 5), Justification::centredLeft, false);
    g.setColour (theme::text);
    g.setFont (theme::font (big ? 20.0f : 15.0f, true));
    g.drawFittedText (value, area, Justification::centredLeft, 1, 0.8f);
}

TopBar::TopBar (Controller& ctrl) : c (ctrl)
{
    for (auto* b : { &start, &playButton, &recordButton, &loop, &punch, &click, &countIn, &follow, &tap, &undoMessage, &dismissMessage })
        addAndMakeVisible (b);
    playButton.setTitle ("Play");
    recordButton.setTitle ("Record");
    start.setTitle ("Go to Start");
    for (auto* r : { &position, &tempo, &signature, &scale, &chord, &cpu })
    {
        addAndMakeVisible (r);
        r->setRepaintsOnMouseActivity (true);
    }
    position.label = "Position";
    tempo.label = "Tempo";
    signature.label = "Time";
    scale.label = "Scale";
    chord.label = "Chord";
    chord.big = true;
    cpu.label = "Delay";
    position.setTooltip ("Click to show bars and beats, minutes, samples or timecode.");
    tempo.setTooltip (theme::wrapTip ("Drag up or down to change the tempo, double-click to type it. Tempo changes and ramps are drawn in the tempo lane (View > Tempo Lane)."));
    signature.setTooltip ("Double-click to change the time signature from the bar the cursor is in.");
    scale.setTooltip (theme::wrapTip ("The scale the song is in. Its notes are shaded in the piano roll, and chords are spelled for it."));
    chord.setTooltip (theme::wrapTip ("The chord sounding at the playhead, from the clips and the keys you're holding."));
    cpu.setTooltip (theme::wrapTip ("How much the plug-ins delay the sound in all. Kalibre lines every track up by this much (plug-in delay compensation)."));

    start.onClick = [this] { c.goToStart(); };
    playButton.onClick = [this] { c.togglePlay(); };
    recordButton.onClick = [this] { c.record(); };
    loop.onClick = [this] { c.toggleLoop(); };
    punch.onClick = [this] { c.apply ("Punch", [] (Project& p) { p.punch.on = ! p.punch.on; }); };
    click.onClick = [this] { c.apply ("Metronome", [] (Project& p) { p.metronome.on = ! p.metronome.on; }); };
    countIn.onClick = [this] { c.apply ("Count-in", [] (Project& p) { p.metronome.countIn = ! p.metronome.countIn; }); };
    follow.onClick = [this] { c.followPlayback = follow.getToggleState(); c.viewChanged(); };
    tap.onClick = [this] { c.tapTempo(); };
    undoMessage.onClick = [this] { c.undo(); };
    dismissMessage.onClick = [this] { c.dismiss(); };

    position.onDown = [this] (const MouseEvent&)
    {
        timeFormat = (TimeFormat) (((int) timeFormat + 1) % 4);
        update();
    };
    tempo.onDown = [this] (const MouseEvent&) { dragStartBpm = c.project.tempo.bpmAt (c.cursor); };
    tempo.onDrag = [this] (const MouseEvent& e)
    {
        const double step = e.mods.isShiftDown() ? 0.01 : 0.1;
        const double bpm = jlimit (TempoMap::minBpm, TempoMap::maxBpm, std::round ((dragStartBpm - e.getDistanceFromDragStartY() * step * 2) * 100.0) / 100.0);
        c.project.tempo.setTempoAt (c.project.tempo.tempos().size() == 1 ? 0 : c.project.tempo.tempos().front().tick, bpm);
        c.refresh();
    };
    tempo.onWheel = [this] (const MouseWheelDetails& w)
    {
        const double bpm = std::round (c.project.tempo.bpmAt (0) + (w.deltaY > 0 ? 1 : -1));
        c.apply ("Tempo", [bpm] (Project& p) { p.tempo.setTempoAt (0, bpm); });
    };
    tempo.onDouble = [this] (const MouseEvent&) { editTempo(); };
    signature.onDouble = [this] (const MouseEvent&) { editSignature(); };
    signature.onDown = [this] (const MouseEvent& e) { if (e.getNumberOfClicks() == 1) {} };
    scale.onDown = [this] (const MouseEvent&) { chooseScale(); };
    chord.onDown = [this] (const MouseEvent&)
    {
        // Clicking the chord plays it.
        const auto pitches = harmony::soundingAt (c.project, c.playhead());
        for (int p : pitches) c.engine.audition (c.focusTrack, p, 90, 0.8);
    };

    c.addChangeListener (this);
    startTimerHz (20);
    update();
}

TopBar::~TopBar() { c.removeChangeListener (this); }

// The bar's width on one row; narrower than this, it takes two (wrapped, never cropped).
static int oneRowWidth (TopBar::TimeFormat)
{
    return theme::px (32 + 40 + 32 + 60 + 64 + 52 + 82 + 130 + 80 + 46 + 56 + 190 + 220 + 70 + 76) + theme::px (6) * 18;
}

int TopBar::preferredHeight() const
{
    const int rows = getWidth() > 0 && getWidth() < oneRowWidth (timeFormat) ? 2 : 1;
    return rows * theme::px (48) + (c.message.isNotEmpty() ? theme::px (32) : 0);
}

void TopBar::paint (Graphics& g)
{
    g.fillAll (theme::ground);
    const int barsHeight = preferredHeight() - (c.message.isNotEmpty() ? theme::px (32) : 0);
    g.setColour (theme::rule);
    g.fillRect (0, barsHeight - 1, getWidth(), 1);
    if (c.message.isNotEmpty())
    {
        auto r = getLocalBounds().withTop (barsHeight).reduced (theme::px (12), 0);
        g.setColour (c.messageIsWarning ? theme::warnText : theme::text);
        g.setFont (theme::font());
        auto textArea = r.withTrimmedRight (undoMessage.isVisible() ? theme::px (170) : theme::px (90));
        g.drawFittedText (c.message, textArea, Justification::centredLeft, 1);
        g.setColour (theme::rule);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
    }
    // The record button is red while recording: shape (a ring) and colour.
    if (c.engine.isRecording())
    {
        g.setColour (theme::warn);
        g.drawRoundedRectangle (recordButton.getBounds().toFloat().expanded (1.5f), 6.0f, 2.0f);
    }
}

void TopBar::resized()
{
    const bool twoRows = getWidth() < oneRowWidth (timeFormat);
    const int rowH = theme::px (48);
    auto r = getLocalBounds().removeFromTop (rowH).reduced (theme::px (8), theme::px (6));
    const int h = r.getHeight();
    const int gap = theme::px (6);
    const int ch = jmin (h, theme::px (28));
    auto place = [&] (Component& comp, int w) { comp.setBounds (r.removeFromLeft (w).withSizeKeepingCentre (w, ch)); r.removeFromLeft (gap); };
    place (start, theme::px (32));
    place (playButton, theme::px (40));
    place (recordButton, theme::px (32));
    r.removeFromLeft (gap);
    place (loop, loop.idealWidth());
    place (punch, punch.idealWidth());
    place (click, click.idealWidth());
    place (countIn, countIn.idealWidth());
    if (twoRows)
    {
        // The second row: where the song is, and what it's in.
        r = getLocalBounds().withTop (rowH).removeFromTop (rowH).reduced (theme::px (8), theme::px (6));
    }
    else r.removeFromLeft (gap);
    position.setBounds (r.removeFromLeft (theme::px (130)));
    r.removeFromLeft (gap);
    tempo.setBounds (r.removeFromLeft (theme::px (80)));
    r.removeFromLeft (gap / 2);
    place (tap, tap.idealWidth());
    signature.setBounds (r.removeFromLeft (theme::px (56)));
    r.removeFromLeft (gap);

    cpu.setBounds (r.removeFromRight (theme::px (76)));
    r.removeFromRight (gap);
    follow.setBounds (r.removeFromRight (follow.idealWidth()).withSizeKeepingCentre (follow.idealWidth(), ch));
    r.removeFromRight (gap);
    const int each = jmax (theme::px (100), (r.getWidth() - gap) / 2);
    scale.setBounds (r.removeFromLeft (jmin (each, theme::px (190))));
    r.removeFromLeft (gap);
    chord.setBounds (r.removeFromLeft (jmin (r.getWidth(), theme::px (220))));

    // Anything that still can't fit is hidden rather than squashed; every one is also in the menus.
    for (auto* comp : std::initializer_list<Component*> { &countIn, &punch, &click, &follow, &cpu, &chord, &scale, &tap, &signature })
        comp->setVisible (comp->getRight() <= getWidth() - theme::px (4) && comp->getWidth() >= theme::px (24));

    const int barsHeight = (twoRows ? 2 : 1) * rowH;
    auto m = getLocalBounds().withTop (barsHeight).reduced (theme::px (12), theme::px (2));
    dismissMessage.setBounds (m.removeFromRight (dismissMessage.idealWidth (theme::smallControl)).withSizeKeepingCentre (dismissMessage.idealWidth (theme::smallControl), theme::px (24)));
    m.removeFromRight (gap);
    undoMessage.setBounds (m.removeFromRight (undoMessage.idealWidth (theme::smallControl)).withSizeKeepingCentre (undoMessage.idealWidth (theme::smallControl), theme::px (24)));
}

void TopBar::changeListenerCallback (ChangeBroadcaster*)
{
    const bool hasMessage = c.message.isNotEmpty();
    undoMessage.setVisible (hasMessage && c.messageCanUndo && c.history.canUndo());
    dismissMessage.setVisible (hasMessage);
    if (auto* p = getParentComponent()) p->resized();
    update();
}

void TopBar::timerCallback() { update(); }

void TopBar::update()
{
    const auto& p = c.project;
    const Tick now = c.playhead();
    const double seconds = p.tempo.secondsAt (now);
    switch (timeFormat)
    {
        case TimeFormat::BarsBeats: position.label = "Bar.Beat"; position.value = p.tempo.formatBarBeat (now); break;
        case TimeFormat::Minutes:   position.label = "Minutes";  position.value = TempoMap::formatMinutes (seconds); break;
        case TimeFormat::Samples:   position.label = "Samples";  position.value = String ((int64) std::llround (seconds * c.engine.sampleRate())); break;
        case TimeFormat::Timecode:  position.label = "Timecode"; position.value = TempoMap::formatTimecode (seconds, 25.0); break;
    }
    tempo.value = String (p.tempo.bpmAt (now), p.tempo.bpmAt (now) == std::floor (p.tempo.bpmAt (now)) ? 0 : 2);
    tempo.label = p.tempo.tempos().size() > 1 ? "Tempo (changes)" : "Tempo";
    const auto& m = p.tempo.meterAt (now);
    signature.value = String (m.numerator) + "/" + String (m.denominator);
    scale.value = harmony::scaleName (p.scale);

    // The chord: the clips at the playhead, and keys held on a keyboard.
    auto pitches = harmony::soundingAt (p, now);
    std::array<int, 128> held {};
    for (auto& e : c.engine.shared.midiCapture.snapshot())
    {
        const int type = e.bytes[0] & 0xF0;
        if (type == 0x90 && e.bytes[2] > 0) held[e.bytes[1] & 127] = 1;
        else if (type == 0x80 || type == 0x90) held[e.bytes[1] & 127] = 0;
    }
    for (int k = 0; k < 128; ++k) if (held[(size_t) k]) pitches.push_back (k);
    auto name = String (harmony::chordName (pitches, p.scale));
    if (name.isEmpty() && pitches.size() == 1) name = String (harmony::noteName (p.scale, pitches.front()));
    chord.value = name.isEmpty() ? ui::u8 ("\xe2\x80\x94") : name;
    chord.setTitle ("Chord: " + (name.isEmpty() ? String ("none") : name));

    const int latency = c.engine.latencySamples();
    cpu.value = String (latency * 1000.0 / jmax (1.0, c.engine.sampleRate()), 1) + " ms";

    playButton.setToggleState (c.engine.isPlaying(), dontSendNotification);
    playButton.setButtonText (c.engine.isPlaying() ? String::fromUTF8 ("\xe2\x96\xa0") : String::fromUTF8 ("\xe2\x96\xb6"));
    playButton.setTitle (c.engine.isPlaying() ? "Stop" : "Play");
    recordButton.setToggleState (c.engine.isRecording(), dontSendNotification);
    loop.setToggleState (p.loop.on, dontSendNotification);
    punch.setToggleState (p.punch.on, dontSendNotification);
    click.setToggleState (p.metronome.on, dontSendNotification);
    countIn.setToggleState (p.metronome.countIn, dontSendNotification);
    follow.setToggleState (c.followPlayback, dontSendNotification);
    for (auto* r : { &position, &tempo, &signature, &scale, &chord, &cpu }) r->repaint();
    repaint();
}

void TopBar::editTempo()
{
    auto* w = new AlertWindow ("Tempo", "BPM at the cursor (a change from there on):", MessageBoxIconType::NoIcon);
    w->addTextEditor ("bpm", String (c.project.tempo.bpmAt (c.cursor), 2));
    w->addButton ("Set", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    ui::present (w, ModalCallbackFunction::create ([this, w] (int r)
    {
        if (r == 1)
        {
            const double bpm = w->getTextEditorContents ("bpm").getDoubleValue();
            if (bpm >= TempoMap::minBpm && bpm <= TempoMap::maxBpm)
            {
                const Tick at = c.cursor <= 0 ? 0 : c.snapDown (c.cursor);
                c.apply ("Tempo", [=] (Project& p) { p.tempo.setTempoAt (at, bpm); });
            }
        }
    }), true);
}

void TopBar::editSignature()
{
    auto* w = new AlertWindow ("Time Signature", "From the bar the cursor is in:", MessageBoxIconType::NoIcon);
    const auto& m = c.project.tempo.meterAt (c.cursor);
    w->addTextEditor ("sig", String (m.numerator) + "/" + String (m.denominator));
    w->addButton ("Set", 1, KeyPress (KeyPress::returnKey));
    w->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    ui::present (w, ModalCallbackFunction::create ([this, w] (int r)
    {
        if (r != 1) return;
        const auto t = w->getTextEditorContents ("sig");
        const int num = t.upToFirstOccurrenceOf ("/", false, false).getIntValue();
        const int den = t.fromFirstOccurrenceOf ("/", false, false).getIntValue();
        const int bar = c.project.tempo.barAt (c.cursor);
        c.apply ("Time Signature", [=] (Project& p) { p.tempo.setMeterAt (bar, num, den); });
        if (c.project.tempo.meterAtBar (bar).numerator != num)
            c.say ("Write it as beats/note value, like 7/8. The note value is 1, 2, 4, 8, 16 or 32.", true);
    }), true);
}

void TopBar::chooseScale()
{
    PopupMenu m, roots, scales;
    for (size_t i = 0; i < scaleview::roots.size(); ++i)
        roots.addItem ((int) i + 1, scaleview::roots[i].name, true, (int) i == c.project.scale.root);
    for (size_t i = 0; i < scaleview::scales.size(); ++i)
        scales.addItem (1000 + (int) i, scaleview::scales[i].name, true, (int) i == c.project.scale.scale);
    m.addSectionHeader ("Root");
    m.addSubMenu ("Root", roots);
    m.addSubMenu ("Scale", scales);
    m.addSeparator();
    m.addItem (2000, "Shade the Scale in the Piano Roll", true, c.project.scale.highlight);
    m.addItem (2001, "Find the Scale from the Notes");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&scale), [this] (int r)
    {
        if (r <= 0) return;
        if (r < 1000) c.apply ("Scale", [r] (Project& p) { p.scale.root = r - 1; });
        else if (r < 2000) c.apply ("Scale", [r] (Project& p) { p.scale.scale = r - 1000; });
        else if (r == 2000) c.apply ("Scale Shading", [] (Project& p) { p.scale.highlight = ! p.scale.highlight; });
        else
        {
            const auto ranked = harmony::rankKeys (harmony::collect (c.project, 0, c.project.endTick()));
            if (ranked.empty() || harmony::collect (c.project, 0, c.project.endTick()).empty())
            {
                c.say ("There are no notes to find a scale from yet.");
                return;
            }
            const auto best = ranked.front();
            c.apply ("Find Scale", [best] (Project& p) { p.scale.root = best.root; p.scale.scale = best.scale; });
            c.say ("The notes fit " + String (harmony::scaleName (c.project.scale)) + " best.", false, true);
        }
    });
}

} // namespace kal
