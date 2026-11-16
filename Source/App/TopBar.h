/*
    TopBar - the transport and what the song is in, along the top of the
    window: Play, Stop, Record, Loop, Punch, the metronome and count-in; the
    position (bars and beats, or minutes, samples or timecode - click it to
    change); tempo and time signature (drag or double-click to type; both
    are automatable from the tempo lane); the scale, and the chord sounding
    now, large, where it can be seen while playing (decision 0015).

    Under it, the message line (decision 0025): what just happened, or what
    went wrong, with Undo and Dismiss - at the top, never along the bottom.
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

    enum class TimeFormat { BarsBeats, Minutes, Samples, Timecode };
    TimeFormat timeFormat = TimeFormat::BarsBeats;

private:
    Controller& c;
    theme::HouseButton start { juce::String::fromUTF8 ("\xe2\x8f\xae"), "Go to the start. (Return)" };
    theme::HouseButton playButton { juce::String::fromUTF8 ("\xe2\x96\xb6"), "Play from the cursor, or stop. (Space)" };
    theme::HouseButton recordButton { juce::String::fromUTF8 ("\xe2\x97\x8f"), "Record onto the armed tracks, or the chosen one. (R)" };
    theme::HouseButton loop { "Loop", "Repeat the loop range while playing. (L)", true };
    theme::HouseButton punch { "Punch", "Record only inside the punch range.", true };
    theme::HouseButton click { "Click", "The metronome, while playing.", true };
    theme::HouseButton countIn { "Count-in", "A bar of clicks before recording starts.", true };
    theme::HouseButton follow { "Follow", "Scroll the view along with the playhead.", true };
    theme::HouseButton tap { "Tap", "Tap a few times on the beat to set the tempo." };

    struct Readout : public juce::Component, public juce::SettableTooltipClient
    {
        juce::String label, value;
        std::function<void (const juce::MouseEvent&)> onDown, onDrag, onDouble;
        std::function<void (const juce::MouseWheelDetails&)> onWheel;
        bool big = false;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent& e) override { if (onDown) onDown (e); }
        void mouseDrag (const juce::MouseEvent& e) override { if (onDrag) onDrag (e); }
        void mouseDoubleClick (const juce::MouseEvent& e) override { if (onDouble) onDouble (e); }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { if (onWheel) onWheel (w); }
    };
    Readout position, tempo, signature, scale, chord, cpu, load;

    // The message line.
    theme::HouseButton undoMessage { "Undo", "Undo what the message describes." };
    theme::HouseButton dismissMessage { "Dismiss", "Hide this message." };

    double dragStartBpm = 120;
    juce::String lastChord;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void update();
    void editTempo();
    void editSignature();
    void chooseScale();
};

} // namespace kal
