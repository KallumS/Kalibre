/*
    TopBar - the transport and what the song is in, along the top of the
    window: Play, Stop, Record, Loop, Punch, the metronome and count-in; the
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

#include "Controller.h"
#include "Theme.h"

namespace kal
{

class TopBar : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit TopBar (Controller&);
    ~TopBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    int preferredHeight() const;
    void mouseDown (const juce::MouseEvent&) override;

    // What can be hidden, by the names the right-click menu uses.
    static const juce::StringArray& hideable();
    void showMenu();
    juce::String shownLine() const { return line; }    // what the message line says (tests)

    enum class TimeFormat { BarsBeats, Minutes, Samples, Timecode };
    TimeFormat timeFormat = TimeFormat::BarsBeats;

private:
    Controller& c;
    // A button whose right-click opens the bar's menu instead of pressing it.
    struct BarButton : public theme::HouseButton
    {
        using theme::HouseButton::HouseButton;
        std::function<void()> onRightClick;
        void mouseDown (const juce::MouseEvent& e) override { if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick(); return; } HouseButton::mouseDown (e); }
        void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) HouseButton::mouseUp (e); }
    };
    BarButton start { juce::String::fromUTF8 ("\xe2\x8f\xae"), "Go to the start. (Return)" };
    BarButton playButton { juce::String::fromUTF8 ("\xe2\x96\xb6"), "Play from the cursor, or stop. (Space)" };
    BarButton recordButton { juce::String::fromUTF8 ("\xe2\x97\x8f"), "Record onto the armed tracks, or the chosen one. (R)" };
    BarButton loop { "Loop", "Repeat the loop range while playing. (L)", true };
    BarButton punch { "Punch", "Record only inside the punch range.", true };
    BarButton click { "Click", "The metronome, while playing.", true };
    BarButton countIn { "Count-in", "A bar of clicks before recording starts.", true };
    BarButton follow { "Follow", "Click: the view scrolls with the playhead. Shift-click: it turns the page when the playhead reaches the edge.", true };
    BarButton tap { "Tap", "Tap a few times on the beat to set the tempo." };

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
    Readout position, tempo, signature, grid, scale, chord, cpu, load;

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
    void chooseGrid();
    std::vector<int> heldPitches() const;
    juce::Component* itemNamed (const juce::String&);
    int lineHeight() const;
};

} // namespace kal
