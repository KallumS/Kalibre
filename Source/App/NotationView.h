/*
    NotationView - one MIDI clip on a staff (decision 0020): read it, and
    write into it. Choose a note value along the top, click a line or space
    to put a note there (spelled from the scale; Option-click sharpens it,
    Shift-click flattens it), right-click a note to remove it. Notes go in at
    the cursor, which then moves on by the value. Under the staff, the
    track's CC lanes, each value under the note it goes with (decision 0060).
*/

#pragma once

#include "CcLanes.h"
#include "Controller.h"
#include "Notation.h"
#include "Theme.h"

namespace kal
{

class NotationView : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit NotationView (Controller&);
    ~NotationView() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    Controller& c;
    juce::OwnedArray<theme::HouseButton> values;
    theme::HouseButton dotted { ".", "Dotted: half as long again.", true };
    theme::HouseButton triplet { "3", "Triplet: two-thirds as long.", true };
    theme::HouseButton rest { "Rest", "Move the cursor on by the value, leaving a rest." };
    notation::Value value = notation::Value::Quarter;
    std::vector<notation::Bar> bars;
    notation::Clef clef = notation::Clef::Treble;

    struct Placed { Tick start; int pitch; juce::Rectangle<float> r; };
    std::vector<Placed> placed;
    std::vector<std::pair<Tick, float>> tickX;   // where each glyph went, for clicking

    float lineGap() const { return (float) theme::px (8); }
    juce::Rectangle<int> staffArea() const;
    float yOfStep (int step) const;
    int stepAt (float y) const;
    Tick tickAtX (float x) const;
    CcLanes lanes;
    int lanesHeight() const;
    float xOfClipTick (Tick) const;       // where a clip tick falls, between the glyphs either side
    Tick clipTickAt (float x) const;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
public:
    // Where the playhead is drawn, or below zero when it isn't (stopped, or outside the clip).
    float playheadX() const;
    float shownPlayheadX = -1;   // where it was last drawn (tests)
};

} // namespace kal
