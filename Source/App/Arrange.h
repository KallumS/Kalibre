/*
    Arrange - the tracks along time: the ruler (bars, the loop range, markers,
    the tempo lane), the chord lane, every track's header and lane, its
    automation lanes under it, and the playhead.

    Clips: click to choose (a MIDI clip opens in the editor below), drag to
    move (onto another track too), drag an edge to trim, Option-drag to
    copy, double-click empty space for a new MIDI clip a bar long. Right-click
    a clip for what else it can do: split, loop, warp, offline effects.

    Tracks: right-click a header for New Version (decision 0006) and the
    rest; the version number on the header chooses between versions.
    Folders fold with their triangle. Audio and MIDI files dropped from the
    Finder land where they are dropped.
*/

#pragma once

#include "Controller.h"
#include "Harmony.h"
#include "Theme.h"
#include "Timeline.h"

namespace kal
{

class Arrange;

class TrackHeader : public juce::Component
{
public:
    TrackHeader (Arrange&, Controller&, Id);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void update (const Track&);
    void refreshButtons();
    void showMenu();

    Id id;

private:
    Arrange& arrange;
    Controller& c;
    theme::HouseButton version { "1", "This track's versions: choose one, or make a new one." };
    theme::HouseButton mute { "M", "Mute.", true };
    theme::HouseButton solo { "S", "Solo.", true };
    theme::HouseButton arm { "R", "Arm for recording.", true };
    theme::HouseButton fold { juce::String::fromUTF8 ("\xe2\x96\xbe"), "Fold the folder shut, or open it." };
    juce::Label name;
    int depth = 0;
    bool dragging = false;
    void versionMenu();
};

class Arrange : public juce::Component,
                private juce::ChangeListener,
                private juce::Timer,
                public juce::FileDragAndDropTarget,
                public juce::DragAndDropTarget,
                public juce::TooltipClient,
                private juce::ScrollBar::Listener
{
public:
    explicit Arrange (Controller&);
    ~Arrange() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    juce::String getTooltip() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    Timeline timeline;
    int headerWidth() const { return theme::px (220); }
    int rulerHeight() const;
    int sectionLaneHeight() const;
    void drawSectionLane (juce::Graphics&, juce::Rectangle<int>);
    void sectionMenu (Id section, Tick at);
    int laneHeightOf (const Track& t) const { return theme::px (juce::jlimit (t.isFolder() ? 28 : 36, 240, t.height)); }

    void zoomToFit();
    void reorder (Id track, int y);   // a header was dragged here

private:
    Controller& c;
    juce::OwnedArray<TrackHeader> headers;
    juce::ScrollBar vbar { true }, hbar { false };
    int scrollY = 0;

    // A row is a track's clips, one of its automation lanes, or one of its takes.
    struct Row
    {
        Id track; int y, h; int automation = -1; int take = -1;
        bool isClips() const { return automation < 0 && take < 0; }
    };
    std::vector<Row> rows;
    int contentHeight = 0;

    std::vector<harmony::ChordSpan> chords;
    juce::uint32 chordsFor = 0;

    // Dragging.
    enum class Drag { None, Cursor, Loop, MoveClip, TrimStart, TrimEnd, Point, Tempo, Marker, WarpMarker, Swipe, FadeIn, FadeOut, Section };
    Id dragSection = 0;          // Shift-dragged in the Sections lane (decision 0037)
    Tick sectionTo = 0;          // where it would start, on a bar line
    Drag drag = Drag::None;
    Id dragClip = 0;
    Tick dragOrigin = 0, dragOffset = 0;
    Id dragTrack = 0;
    bool copying = false;
    Tick previewStart = 0, previewEnd = 0;
    Id previewTrack = 0;
    int dragPoint = -1;
    Project dragBefore;
    Id dragLane = 0;

    std::map<juce::String, std::vector<float>> peaks;   // a file's overview, for drawing

    juce::Rectangle<int> laneArea() const;
    const Row* rowAt (int y) const;
    float xOf (Tick t) const { return (float) headerWidth() + timeline.x (t); }
    Tick tickAt (float x) const { return timeline.tick (x - (float) headerWidth()); }
    const Clip* clipAt (juce::Point<int>, const Track** owner = nullptr) const;

    void layoutRows();
    void drawRuler (juce::Graphics&, juce::Rectangle<int>);
    void drawChordLane (juce::Graphics&, juce::Rectangle<int>);
    void drawTempoLane (juce::Graphics&, juce::Rectangle<int>);
    void drawClip (juce::Graphics&, const Track&, const Clip&, juce::Rectangle<float>, bool chosen);
    void drawAutomation (juce::Graphics&, const Track&, const AutomationLane&, juce::Rectangle<int>);
    void drawTake (juce::Graphics&, const Track&, int take, juce::Rectangle<int>);
    int swipeTake = -1;
    Tick swipeFrom = 0, swipeTo = 0;
    const std::vector<float>& peaksFor (const juce::String& file);
    void clipMenu (Id clip);
    void laneMenu (const Row&, juce::Point<int>);

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    float lastPlayheadX = -1;
    void scrollBarMoved (juce::ScrollBar*, double) override;
};

} // namespace kal
