/*
    PianoRoll - one MIDI clip's notes, in the lower zone.

    The mouse follows the rules in Roll.h (decision 0009): click empty space
    to add a note, right-click a note to remove it, drag a note's body to move
    it and its edge to stretch it, right-drag to remove everything in an area,
    Shift-drag to choose an area. The project's scale shades its rows (the
    root's darker still) when Shade the Scale is on (decision 0015).

    Under the notes, the velocity lane (drag to set how hard each note is
    played; click its name for each note's chance of playing, random velocity
    range or random timing (decision 0061), or to draw a controller there
    instead: modulation, expression, sustain, pitch bend, pressure or any CC) and, on a track with an articulation map, the articulation lane:
    each note's articulation, chosen with a click (decision 0013). Under
    those, the track's CC lanes, one under another (CcLanes, decision 0060).
*/

#pragma once

#include "CcLanes.h"
#include "Controller.h"
#include "NoteOptions.h"
#include "Roll.h"
#include "Theme.h"
#include "Timeline.h"

namespace kal
{

class PianoRoll : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    explicit PianoRoll (Controller&);
    ~PianoRoll() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    void showClip();          // fit the view to the chosen clip
    Timeline timeline;
    float rowHeight = 12.0f;
    float scrollPitch = 84;   // the pitch at the top edge

private:
    Controller& c;
    std::unique_ptr<roll::Gesture> gesture;
    Id noteAnchor = 0;   // the note last clicked without Shift
    Tick lastLength = 0; // a new note's length: the last note added, clicked or stretched
    roll::Outcome preview;
    std::vector<Note> notesCopy;
    bool velocityDrag = false;
    // The lane under the notes: -1 velocity, -2 note chance, -3 random velocity
    // range, -4 random timing (decision 0061), 0-127 a controller, 128 pitch
    // bend, 129 channel pressure, 200-202 MPE.
    int laneKind = -1;
    noteopt::Kind noteKind() const { return (noteopt::Kind) juce::jlimit (0, 3, -laneKind - 1); }
    Tick lastLaneTick = -1;
    void laneMenu();
    void drawLaneValue (const juce::MouseEvent&);
    juce::String laneName() const;
    Project velocityBefore;
    Id shownClip = 0;
    juce::Point<int> downAt;
    bool pastThreshold = false;

    int keyboardWidth() const { return theme::px (56); }
    int rulerHeight() const { return theme::px (20); }
    int velocityHeight() const { return theme::px (64); }
    CcLanes lanes;
    int ccHeight() const;                      // the CC lanes', at the bottom
    int lanesTop() const { return getHeight() - ccHeight(); }
    int articulationHeight() const;
    juce::Rectangle<int> noteArea() const;
    juce::Rectangle<int> velocityArea() const;
    juce::Rectangle<int> articulationArea() const;

    Tick base() const;                         // the clip's content start, in project ticks
    float xOf (Tick rel) const;                // a note tick (clip's own) to x
    Tick relAt (float x) const;
    float yOf (int pitch) const;
    int pitchAt (float y) const;
    roll::Pointer pointerFor (const juce::MouseEvent&) const;
    roll::Settings settings() const;
    void commit (const roll::Outcome&);
    void setVelocityAt (const juce::MouseEvent&);
    void articulationMenu (const Note&);

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
};

} // namespace kal
