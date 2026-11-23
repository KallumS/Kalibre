/*
    TopBar - the transport and what the song is in, along the top of the
    window (the transport buttons are in the TransportBar at the bottom, 0095): the
    position (bars and beats, or minutes, samples or timecode - click it to
    change); tempo and time signature (drag or double-click to type; both
    are automatable from the tempo lane); the scale, and the chord sounding
    now, large, where it can be seen while playing (decision 0015).

    Under it, the message line (decisions 0025, 0070): what just happened,
    or what went wrong, with Undo - at the top, never along the bottom. When
    there's nothing to say it tells what's chosen, or what the pointer is over.

    Right-click the bar to choose what it shows (0070); the Grid is the
    arrangement's; Shift-click Follow turns the page at the edge.
*/

#pragma once

#include "BattlePass.h"
#include "Controller.h"
#include "Theme.h"

namespace kal
{

// A button whose right-click opens the bar's menu instead of pressing it.
struct BarButton : public theme::HouseButton
{
    using theme::HouseButton::HouseButton;
    std::function<void()> onRightClick;
    void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; } HouseButton::mouseDown (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) HouseButton::mouseUp (e); }
};

/*
    TransportBar - Go to Start, Play, Record, Loop, Punch, Click and Count-in,
    in the middle of a bar along the bottom of the window, under the lower
    zone (asked for, 0095: the top bar keeps where the song is and what it's
    in, and the message line). Right-click it for the same menu as the top
    bar's: Loop, Punch, Click and Count-in can be hidden.
*/
class TransportBar : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit TransportBar (Controller&);
    ~TransportBar() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    int preferredHeight() const { return theme::px (44); }
    std::function<void()> onMenu;   // the bar's right-click menu (the top bar's)
    void update();

    BarButton start { juce::String::fromUTF8 ("\xe2\x8f\xae"), "Go to the start. (Return)" };
    BarButton end { juce::String::fromUTF8 ("\xe2\x8f\xad"), "Go to the end: where the last clip on the timeline ends. (End)" };
    BarButton stopButton { juce::String::fromUTF8 ("\xe2\x96\xa0"), "Stop, and go back to the start of the song." };
    BarButton playButton { juce::String::fromUTF8 ("\xe2\x96\xb6"), "Play from the cursor; lit while it plays. Click again to stop where playing began. (Space)" };
    BarButton recordButton { juce::String::fromUTF8 ("\xe2\x97\x8f"), "Record onto the armed tracks, or the chosen one. (R)" };
    BarButton loop { "Loop", "Repeat the loop range while playing. (L)", true };
    BarButton punch { "Punch", "Record only inside the punch range.", true };
    BarButton click { "Click", "The metronome, while playing.", true };
    BarButton countIn { "Count-in", "A bar of clicks before recording starts.", true };

private:
    Controller& c;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { update(); }
    void timerCallback() override { update(); }
};

class TopBar : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit TopBar (Controller&);
    ~TopBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    int preferredHeight() const;
    void mouseDown (const juce::MouseEvent&) override;
    // The message line drawn now, not when the message thread is next free:
    // while it's busy writing an export (the UAT).
    void showNow();

    // What can be hidden, by the names the right-click menu uses.
    static const juce::StringArray& hideable();
    static bool inTransport (const juce::String& name);   // Loop, Punch, Click, Count-in: in the TransportBar (0095)
    void showMenu();
    juce::String shownLine() const { return line; }    // what the message line says (tests)
    // The timer clicked seven times in a row: the secret (DAW Battle Pass, 0111).
    std::function<void()> onSecret;
    juce::Component& timerReadout() { return timer; }   // tests

    enum class TimeFormat { BarsBeats, Minutes, Samples, Timecode };
    TimeFormat timeFormat = TimeFormat::BarsBeats;

private:
    Controller& c;
    BarButton follow { "Follow", "Click: the view scrolls with the playhead. Shift-click: it turns the page when the playhead reaches the edge.", true };
    BarButton tap { "Tap", "Tap a few times on the beat to set the tempo." };
    BarButton random { "Random", "A whole new song by chance, ready to play: 4 to 8 tracks with instruments, effects and music "
                                 "(drums and a tune from Good Idea, chords and a bass from Midi Suggester), and loops from your sounds. "
                                 "Asks to save the song you have first. Shift-click: random tracks added to this song instead, in its key and tempo." };

public:
    // Song versions (0103): 1 and 2, left of Random. 2, not made yet, is in the quieter grey;
    // a click makes it, a copy of 1. Right-click either for New and Delete.
    BarButton songOne { "1", "", true }, songTwo { "2", "", true };
    // Whether the tempo's label fits its box (the UAT, 0133).
    bool tempoLabelFits() const
    {
        return juce::GlyphArrangement::getStringWidth (theme::font (theme::smallPoints), tempo.label) <= (float) (tempo.getWidth() - 16);
    }
private:
    void songVersionMenu (int index);

    struct Readout : public juce::Component, public juce::SettableTooltipClient
    {
        juce::String label, value;
        std::function<void (const juce::MouseEvent&)> onDown, onDrag, onDouble;
        std::function<void (const juce::MouseWheelDetails&)> onWheel;
        std::function<void()> onRightClick;
        bool big = false;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() && onRightClick) { onRightClick(); return; }
            if (onDown) onDown (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override { if (onDrag) onDrag (e); }
        void mouseDoubleClick (const juce::MouseEvent& e) override { if (onDouble) onDouble (e); }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { if (onWheel) onWheel (w); }
    };
    Readout position, tempo, signature, grid, scale, chord, timer, cpu, load;
    SecretClicks secret;

    // The message line.
    theme::HouseButton undoMessage { "Undo", "Undo what the message describes." };
    juce::String line;          // what the message line shows now
    bool lineIsMessage = false;

    // The CPU reading: the highest of the last few seconds, changed only that often.
    double cpuPeak = 0, cpuShown = 0;
    juce::uint32 cpuSince = 0;

    double dragStartBpm = 120;
    juce::String lastChord;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void update();
    void editTempo();
    void editSignature();
    void chooseScale();
public:
    // The Scale menu's Random Scale (0102): any root, any scale, not the one it is; one undo step.
    void randomScale (unsigned seed = 0);
private:
    void chooseGrid();
    std::vector<int> heldPitches() const;
    juce::Component* itemNamed (const juce::String&);
    int lineHeight() const;
};

} // namespace kal
