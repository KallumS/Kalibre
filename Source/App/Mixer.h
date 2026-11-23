/*
    Mixer - every track's channel strip, in the lower zone: volume, pan,
    mute, solo, mono, its meter, its sends, where its sound goes. Folders and
    returns are strips like any other, then the master, then the Cue track
    (decision 0051). Each track's 1 to 4 buttons choose its version (decision
    0050); Shift-click strips to choose several and switch them together.
*/

#pragma once

#include "Controller.h"
#include "Theme.h"

namespace kal
{

class ChannelStrip;

// The master's loudness (decision 0033): momentary, short-term, integrated,
// range, true peak and correlation, with the history of the short-term reading.
class LoudnessPanel : public juce::Component
{
public:
    explicit LoudnessPanel (Controller&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

private:
    Controller& c;
    theme::HouseButton reset { "Reset", "Start measuring the integrated loudness and range again." };
    std::vector<float> history;   // short-term, once a tick
    double m = -100, s = -100, i = -100, lra = 0, tp = -100, corr = 0;
    float heldPeak = -100;
};

class Mixer : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit Mixer (Controller&);
    ~Mixer() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    // Where a track's strip can be seen in the mixer: clipped to the scrolling
    // part when it's in it (tests: the master's is always all there, the UAT).
    juce::Rectangle<int> stripShown (Id) const;

private:
    Controller& c;
    juce::Viewport viewport;
    juce::Component strip;
    juce::OwnedArray<ChannelStrip> strips;
    LoudnessPanel loudness;
    juce::String shape;
    void rebuild();
    void choose (Id, const juce::ModifierKeys&);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
};

} // namespace kal
