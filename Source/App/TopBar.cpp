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
    for (auto* b : { &follow, &tap, &random })
    {
        addAndMakeVisible (b);
        b->onRightClick = [this] { showMenu(); };
    }
    for (int i = 0; i < 2; ++i)
    {
        auto& b = i == 0 ? songOne : songTwo;
        addAndMakeVisible (b);
        b.setTitle ("Song Version " + String (i + 1));
        b.setClickingTogglesState (false);
        b.onClick = [this, i] { c.chooseSongVersion (i); };
        b.onRightClick = [this, i] { songVersionMenu (i); };
    }
    addAndMakeVisible (undoMessage);
    for (auto* r : { &position, &tempo, &signature, &grid, &scale, &chord, &timer, &cpu, &load })
    {
        addAndMakeVisible (r);
        r->setRepaintsOnMouseActivity (true);
        r->onRightClick = [this] { showMenu(); };
    }
    grid.label = "Grid";
    grid.setTooltip (theme::wrapTip ("The arrangement's grid: where clips, the cursor and the loop snap to. Click to choose. The editors have their own, in the lower zone."));
    grid.onDown = [this] (const MouseEvent&) { chooseGrid(); };
    position.label = "Position";
    tempo.label = "Tempo";
    signature.label = "Time";
    scale.label = "Scale";
    chord.label = "Chord";
    chord.big = true;
    timer.label = "Making Music";
    timer.setTooltip (theme::wrapTip ("How long you've been making music in this song: it counts while you edit, play, record or move the mouse in Kalibre, "
                                      "and pauses after 3 minutes with nothing happening (8 minutes while the song plays), or the minutes you choose: right-click the bar. "
                                      "The mouse counts only while Kalibre is the app in front. Saved with the song."));
    cpu.label = "Delay";
    load.label = "CPU";
    load.setTooltip (theme::wrapTip ("How busy the audio is: how much of the time it has for each block it uses, the highest in the last few seconds. "
                                     "Right-click to change how often it changes. The mixer shows each track's share."));
    position.setTooltip ("Click to show bars and beats, minutes, samples or timecode.");
    tempo.setTooltip (theme::wrapTip ("Drag up or down to change the tempo, double-click to type it. Tempo changes and ramps are drawn in the tempo lane (View > Tempo Lane)."));
    signature.setTooltip ("Double-click to change the time signature from the bar the cursor is in.");
    scale.setTooltip (theme::wrapTip ("The scale the song is in. Its notes are shaded in the piano roll, and chords are spelled for it."));
    chord.setTooltip (theme::wrapTip ("The chord you're playing on a MIDI keyboard. The chords of clips are shown in the chord lane: right-click clips > Detect Chords."));
    cpu.setTooltip (theme::wrapTip ("How much the plug-ins delay the sound in all. Kalibre lines every track up by this much (plug-in delay compensation)."));

    follow.onClick = [this]
    {
        // Click: scroll along (or stop following); Shift-click: turn the page at the edge (0070).
        const bool pages = ModifierKeys::getCurrentModifiers().isShiftDown();
        if (pages) { c.followPlayback = ! (c.followPlayback && c.followPages); c.followPages = c.followPlayback; }
        else { c.followPlayback = ! (c.followPlayback && ! c.followPages); c.followPages = false; }
        c.viewChanged();
        update();
    };
    tap.onClick = [this] { c.tapTempo(); };
    random.onClick = [this] { if (c.onRandomSong) c.onRandomSong(); };
    undoMessage.onClick = [this] { c.undo(); };

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
        for (int p : heldPitches()) c.engine.audition (c.focusTrack, p, 90, 0.8);
    };

    c.addChangeListener (this);
    startTimerHz (20);
    update();
}

TopBar::~TopBar() { c.removeChangeListener (this); }

// What can be hidden from the bar (right-click it), by name (0070).
const StringArray& TopBar::hideable()
{
    static const StringArray names { "Loop", "Punch", "Click", "Count-in", "Position", "Tempo", "Tap", "Time Signature",
                                     "Grid", "Scale", "Chord", "Timer", "Follow", "CPU", "Delay", "Song Versions", "Random" };
    return names;
}

// Each item's width on the bar, by name (the scale and chord at their narrowest).
static int widthOf (const String& name)
{
    static const std::map<String, int> w { { "Loop", 52 }, { "Punch", 64 }, { "Click", 56 }, { "Count-in", 82 }, { "Position", 130 },
                                           { "Tempo", 80 }, { "Tap", 46 }, { "Time Signature", 56 }, { "Grid", 64 }, { "Scale", 150 },
                                           { "Chord", 170 }, { "Timer", 104 }, { "Follow", 70 }, { "CPU", 56 }, { "Delay", 76 }, { "Song Versions", 64 }, { "Random", 80 } };
    auto it = w.find (name);
    return it == w.end() ? 0 : it->second;
}

// The bar's width on one row; narrower than this, it takes two (wrapped, never cropped).
static int oneRowWidth (const StringArray& hidden)
{
    int w = 0, n = 1;
    for (auto& name : TopBar::hideable())
        if (! hidden.contains (name) && ! TopBar::inTransport (name)) { w += widthOf (name); ++n; }
    return theme::px (w) + theme::px (6) * n;
}

// The message line: always there, a little smaller than the bar's words (0070).
int TopBar::lineHeight() const { return theme::px (24); }

int TopBar::preferredHeight() const
{
    const int rows = getWidth() > 0 && getWidth() < oneRowWidth (c.hiddenInTopBar) ? 2 : 1;
    return rows * theme::px (48) + lineHeight();
}

void TopBar::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu()) { showMenu(); return; }
    // Clicking the message line puts the message away (it shows what's chosen instead).
    if (e.y >= getHeight() - lineHeight() && c.message.isNotEmpty()) c.dismiss();
}

void TopBar::paint (Graphics& g)
{
    g.fillAll (theme::ground);
    const int barsHeight = getHeight() - lineHeight();
    g.setColour (theme::rule);
    g.fillRect (0, barsHeight - 1, getWidth(), 1);
    if (line.isNotEmpty())
    {
        auto r = getLocalBounds().withTop (barsHeight).reduced (theme::px (12), 0);
        g.setColour (lineIsMessage ? (c.messageIsWarning ? theme::warnText : theme::text) : theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        auto textArea = r.withTrimmedRight (undoMessage.isVisible() ? undoMessage.getWidth() + theme::px (12) : 0);
        g.drawFittedText (line, textArea, Justification::centredLeft, 1);
    }
    g.setColour (theme::rule);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);
}

Component* TopBar::itemNamed (const String& name)
{
    static const char* names[] { "Position", "Tempo", "Tap", "Time Signature", "Grid", "Scale", "Chord", "Timer", "Follow", "CPU", "Delay", "Random" };
    Component* items[] { &position, &tempo, &tap, &signature, &grid, &scale, &chord, &timer, &follow, &load, &cpu, &random };
    if (name == "Song Versions") return &songOne;   // songTwo follows it
    for (size_t i = 0; i < std::size (names); ++i) if (name == names[i]) return items[i];
    return nullptr;
}

void TopBar::resized()
{
    const auto& hidden = c.hiddenInTopBar;
    const bool twoRows = getWidth() < oneRowWidth (hidden);
    const int rowH = theme::px (48);
    auto r = getLocalBounds().removeFromTop (rowH).reduced (theme::px (8), theme::px (6));
    const int h = r.getHeight();
    const int gap = theme::px (6);
    const int ch = jmin (h, theme::px (28));
    auto shown = [&] (const char* name) { return ! hidden.contains (name); };
    auto place = [&] (Component& comp, int w) { comp.setBounds (r.removeFromLeft (w).withSizeKeepingCentre (w, ch)); r.removeFromLeft (gap); };
    auto readout = [&] (Component& comp, int w) { comp.setBounds (r.removeFromLeft (w)); r.removeFromLeft (gap); };
    // The Random button, at the very top right, on the first row however many there are (0101).
    if (shown ("Random"))
    {
        const int rw = random.idealWidth();
        random.setBounds (r.removeFromRight (rw).withSizeKeepingCentre (rw, ch));
        r.removeFromRight (gap * 2);
    }
    // Song versions 1 and 2 (0103), beside it.
    if (shown ("Song Versions"))
    {
        const int side = ch + theme::px (4);
        songTwo.setBounds (r.removeFromRight (side).withSizeKeepingCentre (side, ch));
        r.removeFromRight (theme::px (3));
        songOne.setBounds (r.removeFromRight (side).withSizeKeepingCentre (side, ch));
        r.removeFromRight (gap * 2);
    }
    // Where the song is, and what it's in (the transport is at the bottom of the window, 0095).
    if (shown ("Position")) readout (position, theme::px (130));
    if (shown ("Tempo")) { tempo.setBounds (r.removeFromLeft (theme::px (80))); r.removeFromLeft (gap / 2); }
    if (shown ("Tap")) place (tap, tap.idealWidth());
    if (shown ("Time Signature")) readout (signature, theme::px (56));
    if (shown ("Grid")) readout (grid, theme::px (64));
    // Too narrow for one row: the scale, the chord and the rest take a second.
    if (twoRows) r = getLocalBounds().withTop (rowH).removeFromTop (rowH).reduced (theme::px (8), theme::px (6));
    else r.removeFromLeft (gap);

    if (shown ("Delay")) { cpu.setBounds (r.removeFromRight (theme::px (76))); r.removeFromRight (gap); }
    if (shown ("CPU")) { load.setBounds (r.removeFromRight (theme::px (56))); r.removeFromRight (gap); }
    if (shown ("Follow"))
    {
        const int fw = follow.idealWidth();
        follow.setBounds (r.removeFromRight (fw).withSizeKeepingCentre (fw, ch));
        r.removeFromRight (gap);
    }
    // The timer just right of the chord (0091), its room kept before the chord takes the rest.
    const int timerWidth = shown ("Timer") ? theme::px (104) + gap : 0;
    const int each = jmax (theme::px (100), (r.getWidth() - gap - timerWidth) / 2);
    if (shown ("Scale")) { scale.setBounds (r.removeFromLeft (jmin (each, theme::px (190)))); r.removeFromLeft (gap); }
    if (shown ("Chord")) { chord.setBounds (r.removeFromLeft (jmax (0, jmin (r.getWidth() - timerWidth, theme::px (220))))); r.removeFromLeft (gap); }
    if (shown ("Timer")) timer.setBounds (r.removeFromLeft (jmin (r.getWidth(), theme::px (104))));

    // Hidden by choice; and anything that still can't fit is hidden rather than squashed.
    for (auto& name : hideable())
        if (auto* comp = itemNamed (name))
            comp->setVisible (shown (name.toRawUTF8()) && comp->getRight() <= getWidth() - theme::px (4) && comp->getWidth() >= theme::px (24));
    songTwo.setVisible (songOne.isVisible());

    const int barsHeight = (twoRows ? 2 : 1) * rowH;
    auto m = getLocalBounds().withTop (barsHeight).reduced (theme::px (12), theme::px (2));
    const int uw = undoMessage.idealWidth (theme::smallControl);
    undoMessage.setBounds (m.removeFromRight (uw).withSizeKeepingCentre (uw, theme::px (20)));
}

void TopBar::changeListenerCallback (ChangeBroadcaster*)
{
    resized();
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

    // The chord: what's played on a MIDI keyboard, only (0070); clips' chords are in the chord lane.
    const auto pitches = heldPitches();
    auto name = String (harmony::chordName (pitches, p.scale));
    if (name.isEmpty() && pitches.size() == 1) name = String (harmony::noteName (p.scale, pitches.front()));
    chord.value = name.isEmpty() ? ui::u8 ("\xe2\x80\x94") : name;
    {
        // The timer (0091): a stopwatch of the song's music-making; "Paused" while away.
        const String shown (WorkClock::format (c.workClock.active));
        const String label = c.workClock.idle ? "Paused" : "Making Music";
        if (timer.value != shown || timer.label != label) { timer.value = shown; timer.label = label; timer.repaint(); }
    }
    chord.setTitle ("Chord: " + (name.isEmpty() ? String ("none") : name));
    {
        // Song versions (0103): the one playing lit; 2 in the quieter grey until it's made.
        const int playing = p.songVersion;
        const bool two = p.songVersions() == 2;
        songOne.setToggleState (playing == 0, dontSendNotification);
        songTwo.setToggleState (playing == 1, dontSendNotification);
        songTwo.setColour (TextButton::textColourOffId, two ? theme::text : theme::textSecondary);
        const String tipOne = playing == 0 ? "Song version 1, playing." : "Song version 1. Click to play it: its plug-ins load again as they were, and version 2's are unloaded.";
        const String tipTwo = ! two ? "Song version 2. Click to make it: a copy of the whole song to change freely, with 1 kept as it is. "
                                      "The version not playing has its plug-ins unloaded and its audio let go of, so it costs almost nothing."
                            : playing == 1 ? "Song version 2, playing."
                                           : "Song version 2. Click to play it: its plug-ins load again as they were, and version 1's are unloaded.";
        if (songOne.getTooltip() != theme::wrapTip (tipOne + " Right-click for more.")) songOne.setTip (tipOne + " Right-click for more.");
        if (songTwo.getTooltip() != theme::wrapTip (tipTwo + " Right-click for more.")) songTwo.setTip (tipTwo + " Right-click for more.");
    }

    const int latency = c.engine.latencySamples();
    cpu.value = String (latency * 1000.0 / jmax (1.0, c.engine.sampleRate()), 1) + " ms";
    // Measured as often as before; shown as the highest of the last few seconds, changed only that often.
    cpuPeak = jmax (cpuPeak, c.engine.cpu());
    const auto nowMs = Time::getMillisecondCounter();
    if (cpuSince == 0 || nowMs - cpuSince >= (uint32) (1000 * jmax (1, c.cpuEverySeconds)))
    {
        cpuShown = cpuPeak;
        cpuPeak = 0;
        cpuSince = nowMs;
    }
    load.value = String (roundToInt (cpuShown * 100.0)) + "%";

    // The message line: a message until things move on, then what's chosen or pointed at.
    c.retireMessage();
    lineIsMessage = c.message.isNotEmpty();
    line = lineIsMessage ? c.message : c.infoLine();
    const bool undoable = lineIsMessage && c.messageCanUndo && c.history.canUndo();
    if (undoMessage.isVisible() != undoable) undoMessage.setVisible (undoable);

    follow.setToggleState (c.followPlayback, dontSendNotification);
    const auto followText = c.followPlayback && c.followPages ? String ("Follow: Page") : String ("Follow");
    if (follow.getButtonText() != followText) { follow.setButtonText (followText); resized(); }
    {
        const char* names[] { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
        String g = String (c.arrangeGrid) + " ticks";
        for (int i = 0; i < 7; ++i)
        {
            const Tick v = PPQ * 4 >> i;
            if (v == c.arrangeGrid) g = names[i];
            else if (v * 2 / 3 == c.arrangeGrid) g = String (names[i]) + "T";
        }
        grid.value = c.snap ? g : String ("Off");
    }
    for (auto* r : { &position, &tempo, &signature, &grid, &scale, &chord, &cpu, &load }) r->repaint();
    repaint();
}

std::vector<int> TopBar::heldPitches() const
{
    std::array<int, 128> held {};
    for (auto& e : c.engine.shared.midiCapture.snapshot())
    {
        const int type = e.bytes[0] & 0xF0;
        if (type == 0x90 && e.bytes[2] > 0) held[e.bytes[1] & 127] = 1;
        else if (type == 0x80 || type == 0x90) held[e.bytes[1] & 127] = 0;
    }
    std::vector<int> out;
    for (int k = 0; k < 128; ++k) if (held[(size_t) k]) out.push_back (k);
    return out;
}

// Right-click the bar: what it shows, and how often the CPU reading changes (0070).
void TopBar::songVersionMenu (int index)
{
    PopupMenu m;
    const bool two = c.project.songVersions() == 2;
    m.addItem (index == 0 ? "Play Song Version 1" : two ? "Play Song Version 2" : "New Song Version", index != c.project.songVersion,
               false, [this, index] { c.chooseSongVersion (index); });
    m.addItem ("Delete Song Version " + String (index + 1), two, false, [this, index] { c.deleteSongVersion (index); });
    m.addSeparator();
    m.addItem ("Show in the Bar...", [this] { showMenu(); });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (index == 0 ? &songOne : &songTwo));
}

void TopBar::showMenu()
{
    PopupMenu m;
    m.addSectionHeader ("Show in the Bar");
    for (auto& name : hideable())
        m.addItem (name, true, ! c.hiddenInTopBar.contains (name), [this, name]
        {
            if (c.hiddenInTopBar.contains (name)) c.hiddenInTopBar.removeString (name);
            else c.hiddenInTopBar.add (name);
            resized();
            if (auto* p = getParentComponent()) p->resized();
            repaint();
        });
    m.addItem ("Show Everything", ! c.hiddenInTopBar.isEmpty(), false, [this]
    {
        c.hiddenInTopBar.clear();
        resized();
        if (auto* p = getParentComponent()) p->resized();
    });
    PopupMenu every;
    for (int s : { 1, 2, 5 })
        every.addItem (s == 1 ? String ("Every Second") : "Every " + String (s) + " Seconds", true, c.cpuEverySeconds == s,
                       [this, s] { c.cpuEverySeconds = s; cpuSince = 0; update(); });
    m.addSeparator();
    m.addSubMenu ("Change the CPU Reading", every);
    // The timer's "minutes before you count as away", as the battle pass's Stats page has it (0091).
    PopupMenu away;
    const int now = (int) std::lround (c.workClock.awayAfter / 60.0);
    for (int mins : { 1, 2, 3, 5, 10, 15 })
        away.addItem (mins == 1 ? String ("1 Minute") : String (mins) + " Minutes", true, now == mins,
                      [this, mins] { c.workClock.setAwayMinutes (mins); update(); });
    m.addSubMenu ("Pause the Timer After", away);
    m.showMenuAsync (PopupMenu::Options());
}

//==============================================================================
bool TopBar::inTransport (const String& name)
{
    return name == "Loop" || name == "Punch" || name == "Click" || name == "Count-in";
}

TransportBar::TransportBar (Controller& ctrl) : c (ctrl)
{
    setTitle ("Transport");
    for (auto* b : { &start, &end, &stopButton, &playButton, &recordButton, &loop, &punch, &click, &countIn })
    {
        addAndMakeVisible (b);
        b->onRightClick = [this] { if (onMenu) onMenu(); };
    }
    playButton.setTitle ("Play");
    recordButton.setTitle ("Record");
    start.setTitle ("Go to Start");
    start.onClick = [this] { c.goToStart(); };
    end.setTitle ("Go to End");
    end.onClick = [this] { c.goToEnd(); };
    stopButton.setTitle ("Stop");
    stopButton.onClick = [this] { c.stopAndGoToStart(); };
    playButton.onClick = [this] { c.togglePlay(); };
    recordButton.onClick = [this] { c.record(); };
    loop.onClick = [this] { c.toggleLoop(); };
    punch.onClick = [this] { c.apply ("Punch", [] (Project& p) { p.punch.on = ! p.punch.on; }); };
    click.onClick = [this] { c.apply ("Metronome", [] (Project& p) { p.metronome.on = ! p.metronome.on; }); };
    countIn.onClick = [this] { c.apply ("Count-in", [] (Project& p) { p.metronome.countIn = ! p.metronome.countIn; }); };
    c.addChangeListener (this);
    startTimerHz (20);
    update();
}

TransportBar::~TransportBar() { c.removeChangeListener (this); }

void TransportBar::mouseDown (const MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onMenu) onMenu();
}

void TransportBar::update()
{
    const auto& p = c.project;
    playButton.setToggleState (c.engine.isPlaying(), dontSendNotification);
    playButton.setTitle ("Play");   // always ▶, lit while playing: Stop (■) is its own button (0107)
    const bool recording = c.engine.isRecording();
    if (recordButton.getToggleState() != recording) { recordButton.setToggleState (recording, dontSendNotification); repaint(); }
    loop.setToggleState (p.loop.on, dontSendNotification);
    punch.setToggleState (p.punch.on, dontSendNotification);
    click.setToggleState (p.metronome.on, dontSendNotification);
    countIn.setToggleState (p.metronome.countIn, dontSendNotification);
    bool changed = false;
    for (auto* b : { &loop, &punch, &click, &countIn })
        changed |= b->isVisible() == c.hiddenInTopBar.contains (b->getButtonText());
    if (changed) resized();
}

void TransportBar::paint (Graphics& g)
{
    g.fillAll (theme::ground);
    g.setColour (theme::rule);
    g.fillRect (0, 0, getWidth(), 1);
    // The record button is red while recording: shape (a ring) and colour.
    if (c.engine.isRecording())
    {
        g.setColour (theme::warn);
        g.drawRoundedRectangle (recordButton.getBounds().toFloat().expanded (1.5f), 6.0f, 2.0f);
    }
}

void TransportBar::resized()
{
    // In the middle: Go to Start, Go to End, Stop, Play, Record; a gap; Loop, Punch, Click, Count-in.
    const int gap = theme::px (6);
    const int ch = theme::px (28);
    std::vector<std::pair<Component*, int>> row { { &start, theme::px (32) }, { &end, theme::px (32) }, { &stopButton, theme::px (32) },
                                                  { &playButton, theme::px (40) }, { &recordButton, theme::px (32) } };
    std::vector<std::pair<Component*, int>> toggles;
    for (auto* b : { &loop, &punch, &click, &countIn })
    {
        const bool shown = ! c.hiddenInTopBar.contains (b->getButtonText());
        b->setVisible (shown);
        if (shown) toggles.push_back ({ b, b->idealWidth() });
    }
    auto width = [&]
    {
        int total = 0;
        for (auto& [comp, w] : row) total += w + gap;
        if (! toggles.empty()) total += gap * 2;
        for (auto& [comp, w] : toggles) total += w + gap;
        return total - gap;
    };
    // Too narrow (a small window, large text): the toggles go from the right, rather
    // than off the edge; they're in the Transport menu still.
    while (! toggles.empty() && width() > getWidth() - theme::px (8))
    {
        toggles.back().first->setVisible (false);
        toggles.pop_back();
    }
    const int total = width();
    int x = (getWidth() - total) / 2;
    const int y = (getHeight() - ch) / 2 + 1;
    for (auto& [comp, w] : row) { comp->setBounds (x, y, w, ch); x += w + gap; }
    x += gap * 2;
    for (auto& [comp, w] : toggles) { comp->setBounds (x, y, w, ch); x += w + gap; }
}

// The arrangement's grid (0070): 1/1 to 1/64, triplets, or off.
void TopBar::chooseGrid()
{
    PopupMenu m, triplets;
    const char* names[] { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
    auto set = [this] (Tick v) { c.arrangeGrid = v; c.snap = true; c.viewChanged(); update(); };
    for (int i = 0; i < 7; ++i)
    {
        const Tick v = PPQ * 4 >> i;
        m.addItem (names[i], true, c.snap && c.arrangeGrid == v, [set, v] { set (v); });
        if (i > 0) triplets.addItem (String (names[i]) + " Triplet", true, c.snap && c.arrangeGrid == v * 2 / 3, [set, v] { set (v * 2 / 3); });
    }
    m.addSubMenu ("Triplets", triplets);
    m.addSeparator();
    m.addItem ("Snap to the Grid", true, c.snap, [this] { c.snap = ! c.snap; c.viewChanged(); update(); });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&grid));
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

void TopBar::randomScale (unsigned seed)
{
    Random rng (seed != 0 ? (int64) seed : Time::currentTimeMillis());
    const auto was = c.project.scale;
    auto now = was;
    for (int tries = 0; tries < 20 && now.root == was.root && now.scale == was.scale; ++tries)
    {
        now.root = rng.nextInt ((int) scaleview::roots.size());
        now.scale = rng.nextInt ((int) scaleview::scales.size());
    }
    c.apply ("Random Scale", [now] (Project& p) { p.scale.root = now.root; p.scale.scale = now.scale; });
    c.say ("The scale is now " + String (harmony::scaleName (c.project.scale)) + ".", false, true);
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
    m.addItem (2002, "Random Scale");   // any root, any scale (0102)
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&scale), [this] (int r)
    {
        if (r <= 0) return;
        if (r < 1000) c.apply ("Scale", [r] (Project& p) { p.scale.root = r - 1; });
        else if (r < 2000) c.apply ("Scale", [r] (Project& p) { p.scale.scale = r - 1000; });
        else if (r == 2000) c.apply ("Scale Shading", [] (Project& p) { p.scale.highlight = ! p.scale.highlight; });
        else if (r == 2002) randomScale();
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
