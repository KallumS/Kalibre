/*
    PatternEditors - the drum grid, the step sequencer and the Euclidean
    sequencer, each another way into the same clip's notes (decision 0014).
    Every click writes the notes straight back, as one undo step. Each shows
    and changes only the notes it made, so all of them, and the piano roll,
    add to one clip (decision 0059). While it plays, the step being played
    is lit.
*/

#pragma once

#include "Controller.h"
#include "Patterns.h"
#include "Theme.h"

namespace kal
{

// Drums: a row for each drum, a column for each step. Click a cell to hit it,
// Shift-click for an accent, right-click to clear it, drag up or down on a
// hit to change how hard.
class DrumGrid : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit DrumGrid (Controller&);
    ~DrumGrid() override;
    int preferredHeight() const { return (int) grid.lanes.size() * theme::px (24) + 8; }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

protected:
    Controller& c;
    patterns::Grid grid;
    int dragLane = -1, dragStep = -1, dragStartVelocity = 0;
    Project before;

    void read();
    void write (const juce::String& label);
    int stepsFor (const Clip&) const;
    juce::Rectangle<int> cellArea() const;
    int nameWidth() const { return theme::px (96); }
    int lit = -1;   // the step being played, or -1
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
};

// Steps: one note a step. Click to switch a step on or off, drag up or down
// to change its note (in the scale when the scale is shaded), Option-drag for
// its velocity, double-click to tie it into the next.
class StepSequencer : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit StepSequencer (Controller&);
    ~StepSequencer() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    Controller& c;
    patterns::StepSequence seq;
    int dragStep = -1, dragPitch = 60, dragVelocity = 100;
    bool changed = false;
    Project before;
    void read();
    void write (const juce::String&);
    int stepAt (int x) const;
    int lit = -1;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
};

// Euclidean: for each drum, how many steps, how many hits spread as evenly as
// they go, and where the pattern starts. Shown as a circle and written to the
// clip as you turn the dials.
class EuclidEditor : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit EuclidEditor (Controller&);
    ~EuclidEditor() override;
    int preferredHeight() const { return theme::px (48) + (int) rows.size() * theme::px (90); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Controller& c;
    struct Row
    {
        int pitch = 36;
        juce::String name;
        int steps = 16, pulses = 4, rotation = 0, velocity = 100;
        std::unique_ptr<juce::Slider> stepsKnob, pulsesKnob, rotationKnob;
        std::unique_ptr<theme::HouseButton> drum;
    };
    std::vector<std::unique_ptr<Row>> rows;
    theme::HouseButton addRow { "Add Drum", "Add another Euclidean rhythm on another drum." };
    void build();
    void apply (Row&);
    std::vector<int> lit;   // each row's step being played
    std::vector<int> litSteps() const;
    void changeListenerCallback (juce::ChangeBroadcaster*) override {}
    void timerCallback() override;
};

} // namespace kal
