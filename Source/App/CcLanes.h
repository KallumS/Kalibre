/*
    CcLanes - the chosen clip's controller lanes, one under another, under
    the piano roll's notes and under the notation (decision 0060).

    A lane for each CC the track shows (CC1 and CC11 to start with), and for
    any other the clip already uses. Drag in a lane to draw its values, as a
    line that holds until the next point; right-drag to clear. Click a lane's
    name for another controller, Clear Lane or Remove Lane. Each drawing is
    one undo step.

    Where a tick is across the view is the view's to say (the piano roll's
    time is even; notation's follows its notes), so it's handed in.
*/

#pragma once

#include "Controller.h"
#include "Theme.h"

namespace kal
{

class CcLanes : public juce::Component
{
public:
    explicit CcLanes (Controller&);

    // A clip tick (as its notes and events count) to x, and back.
    std::function<float (Tick)> xOf;
    std::function<Tick (float)> tickAt;
    int gutter = 56;    // the width on the left for the lanes' names

    // The CCs shown: the track's lanes, then any other the clip uses.
    std::vector<int> shown() const;
    // How tall the lanes want to be, given at most `room`.
    int heightFor (int room) const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // The menu of lanes to show or hide on the chosen clip's track.
    static void lanesMenu (Controller&, juce::Component* target);
    static juce::PopupMenu lanesOf (Controller&);

private:
    Controller& c;
    int drawing = -1;          // the CC being drawn, or -1
    Tick lastTick = -1;
    Project before;

    juce::Rectangle<int> laneArea (int index) const;
    int laneAt (int y) const;
    void drawAt (const juce::MouseEvent&);
    void laneMenu (int cc);
};

} // namespace kal
