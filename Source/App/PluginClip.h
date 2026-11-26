/*
    PluginClip - a window onto a plug-in's window: scaled to fit, and showing
    only the part inside it (decision 0058).

    On the Mac a plug-in's window is a native view. Nothing Kalibre draws can
    cover, clip or scale it: a tall plug-in hung out of the bottom of its
    panel, over the rack's scroll bar and the window's edge. Here it's put
    inside a native view that clips, and scales everything in it the same
    both ways (so it shrinks to fit, never stretched), even a plug-in that
    ignores being asked for another size. Elsewhere it's an ordinary
    component with a transform.

    Also: two-finger swipes sideways over an area (the device rack), even
    over the plug-ins' own windows, which would otherwise take them.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace kal
{

class PluginClip : public juce::Component
{
public:
    PluginClip();
    ~PluginClip() override;

    // What's shown: a component holding the plug-in's window. Not owned; it
    // must outlive this, or be taken out first with setContent (nullptr).
    void setContent (juce::Component*);
    juce::Component* getContent() const { return content; }

    // Where the content's top left is, in this component's coordinates, and
    // how much it's scaled (1: its own size; 0.5: half as wide and as tall).
    void setContentPlacement (juce::Point<int>, float scale);
    // A point in this component, in the content's own units (a drop over the plug-in's window).
    juce::Point<float> toContent (juce::Point<float> p) const { return (p - position.toFloat()) / (scale > 0 ? scale : 1.0f); }

    void resized() override;

    struct Native;

private:
    juce::Component* content = nullptr;
    juce::Point<int> position;
    float scale = 1.0f;
    void place();
    std::unique_ptr<Native> native;
};

// Calls `moved` with the distance in pixels (positive: the content should
// move right, as the fingers do) for each sideways two-finger swipe over
// `area`, wherever in it, plug-in windows included; it says whether it used
// it (false: the swipe goes on to whatever is under it). Sideways only: an
// up-and-down swipe goes where it went before. Watching stops when the
// returned handle goes. Nothing happens off the Mac, where the area's own
// mouseWheelMove sees the swipes.
std::shared_ptr<void> watchSidewaysSwipes (juce::Component& area, std::function<bool (float)> moved);

#if JUCE_LINUX
// X's errors (a plug-in's window gone, a size it can't take) noted and passed
// over: X's own handler ends the program (0139). Set as Kalibre starts, and
// again before Kalibre's own X calls, in case a plug-in left its own in place.
void keepXErrorsQuiet();
#endif

} // namespace kal
