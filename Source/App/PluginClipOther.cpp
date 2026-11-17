#include "PluginClip.h"

namespace kal
{
// Off the Mac, the plug-in's window is an ordinary child, clipped as any is.
struct PluginClip::Native {};

PluginClip::PluginClip() { setInterceptsMouseClicks (false, true); }
PluginClip::~PluginClip() { setContent (nullptr); }

void PluginClip::setContent (juce::Component* c)
{
    if (c == content) return;
    if (content != nullptr) removeChildComponent (content);
    content = c;
    if (content != nullptr)
    {
        addAndMakeVisible (content);
        place();
    }
}

void PluginClip::setContentPlacement (juce::Point<int> p, float s)
{
    position = p;
    scale = s;
    place();
}

void PluginClip::place()
{
    if (content == nullptr) return;
    content->setTopLeftPosition (0, 0);
    content->setTransform (juce::AffineTransform::scale (scale).translated ((float) position.x, (float) position.y));
}

void PluginClip::resized() {}

std::shared_ptr<void> watchSidewaysSwipes (juce::Component&, std::function<bool (float)>) { return {}; }
} // namespace kal
