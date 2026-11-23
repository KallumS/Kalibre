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
#include "NoteOptions.h"
#include "Patterns.h"
#include "Theme.h"

namespace kal
{

// Swing and gate for a pattern (decision 0063): two small sliders, each
// named and showing its value. A drag is one undo step: `began`, `changed`
// as it moves, `ended`.
class FeelControls : public juce::Component
{
public:
    FeelControls();
    std::function<void()> began, ended;
    std::function<void (double swing, double gate)> changed;
    void set (const PatternFeel&);
    int idealWidth() const;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Slider swing, gate;
    int labelWidth() const { return theme::px (44); }
    int valueWidth() const { return theme::px (40); }
    int sliderWidth() const { return theme::px (90); }
};

// Drums: a row for each drum, a column for each step (swing, gate and each
// step's repeat, decision 0063). Click a cell to hit it,
// Shift-click for an accent, right-click to clear it, drag up or down on a
// hit to change how hard - or, chosen from the menu at the top left, its
// chance of playing, random velocity range or random timing (decision 0061).
class DrumGrid : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit DrumGrid (Controller&);
    ~DrumGrid() override;
    int preferredHeight() const { return (int) grid.lanes.size() * theme::px (24) + 8 + topHeight(); }
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

protected:
    Controller& c;
    patterns::Grid grid;
    int dragLane = -1, dragStep = -1;
    double dragStartValue = 0;
    noteopt::Kind kind = noteopt::Kind::Velocity;
    theme::HouseButton option { juce::String::fromUTF8 ("Velocity \xe2\x96\xbe"), "What dragging a hit sets: its velocity, its chance of playing, or how far its velocity or timing may wander." };
    theme::HouseButton randomise { "Random", "A new drum groove from Good Idea, every click: replaces this grid's hits (Undo puts them back)." };
    void random();
    FeelControls feel;
    int topHeight() const { return theme::px (34); }
    double valueAt (int lane, int step) const;
    void setValueAt (int lane, int step, double);
    Project before;

    void read();
    void write (const juce::String& label);
    std::vector<patterns::Lane> kitLanes (int steps) const;
    int stepsFor (const Clip&) const;
    juce::Rectangle<int> cellArea() const;
    int nameWidth() const { return theme::px (96); }
    int lit = -1;   // the step being played, or -1
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
};

// Steps: one note a step. Click to switch a step on or off, drag up or down
// to change its note (in the scale when the scale is shaded), Option-drag for
// its velocity, double-click to tie it into the next. The menu at the top
// left chooses what Option-drag sets: velocity, chance of playing, random
// velocity range or random timing; with any but velocity, a plain drag sets
// it too (decision 0061).
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
    double dragValue = 0;
    noteopt::Kind kind = noteopt::Kind::Velocity;
    theme::HouseButton option { juce::String::fromUTF8 ("Velocity \xe2\x96\xbe"), "What Option-drag sets on a step: its velocity, its chance of playing, or how far its velocity or timing may wander." };
    juce::Rectangle<int> cellsArea() const;
    void resized() override;
    theme::HouseButton randomise { "Random", "A new tune from Good Idea in the song's key, every click: replaces these steps (Undo puts them back)." };
    void random();
    FeelControls feel;
    double gateSetting = 0.5;     // the gate every step takes (decision 0063)
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
// clip as you turn the dials. Random rolls them all (decision 0062).
class EuclidEditor : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit EuclidEditor (Controller&);
    ~EuclidEditor() override;
    // The top line, its gaps and the margins (60), then a row each: the last row whole (0133).
    int preferredHeight() const { return theme::px (60) + (int) rows.size() * theme::px (90); }
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
    theme::HouseButton randomise { "Random", "New steps, hits and rotation for every drum, every click: written into the clip (Undo puts back what was there)." };
    void random();
    void applyAll (const juce::String& label);   // empty: no undo step (a drag in progress)
    FeelControls feel;
    Project before;
    patterns::Grid gridFor (const Row&, const PatternFeel&) const;
    void build();
    void apply (Row&);
    std::vector<int> lit;   // each row's step being played
    std::vector<int> litSteps() const;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
};

} // namespace kal
