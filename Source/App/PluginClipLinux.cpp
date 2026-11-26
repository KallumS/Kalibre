// The plug-in's window in the rack on Linux (decisions 0058, 0139).
//
// A plug-in's own window (a VST3's, an LV2's, a CLAP's) is an X11 window
// embedded in Kalibre's (JUCE's XEmbedComponent): X draws it over whatever
// Kalibre draws, and JUCE can neither clip nor scale it. So here, as Windows
// does with window regions (PluginClipWin.cpp):
// - Kalibre's own devices (drawn by JUCE) are scaled with a transform;
// - every embedded window inside the content is given a shape (X's SHAPE
//   extension): only the part inside this clip shows, never over the rack's
//   edge, its scroll bar or the transport. Checked as it's placed and ten
//   times a second, for plug-ins that open or move their windows later.
// Shrinking a plug-in's own drawing is asked of the plug-in (VST3's content
// scale, in DeviceRack); one that can't is shown in part, clipped.

#include "PluginClip.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <X11/Xlib.h>
#include <X11/extensions/shape.h>

#include <cstdio>
#include <vector>

namespace kal
{
namespace
{
int quietly (Display*, XErrorEvent* e)
{
    char what[128] {};
    if (e != nullptr) std::snprintf (what, sizeof (what), "X error %d (request %d) passed over\n", (int) e->error_code, (int) e->request_code);
    std::fputs (what, stderr);
    return 0;
}
} // namespace

void keepXErrorsQuiet() { XSetErrorHandler (quietly); }

struct PluginClip::Native : private juce::Timer
{
    explicit Native (PluginClip& c) : clip (c) { startTimerHz (10); }
    ~Native() override
    {
        stopTimer();
        release();
        if (display != nullptr) XCloseDisplay (display);
    }

    PluginClip& clip;
    Display* display = nullptr;
    std::vector<::Window> shaped;   // windows given a shape, to give back their whole when they leave

    static void embedsIn (juce::Component& c, std::vector<juce::XEmbedComponent*>& out)
    {
        for (auto* child : c.getChildren())
        {
            if (auto* e = dynamic_cast<juce::XEmbedComponent*> (child)) out.push_back (e);
            embedsIn (*child, out);
        }
    }

    bool open()
    {
        if (display == nullptr) display = XOpenDisplay (nullptr);
        if (display == nullptr) return false;
        int event = 0, error = 0;
        return XShapeQueryExtension (display, &event, &error) != 0;
    }

    void whole (::Window w) { XShapeCombineMask (display, w, ShapeBounding, 0, 0, None, ShapeSet); }

    void apply()
    {
        auto* content = clip.getContent();
        if (content == nullptr || clip.getPeer() == nullptr || ! clip.isShowing()) { release(); return; }
        std::vector<juce::XEmbedComponent*> embeds;
        embedsIn (*content, embeds);
        if (embeds.empty() && shaped.empty()) return;
        if (! open()) return;
        keepXErrorsQuiet();   // a window gone meanwhile
        std::vector<::Window> now;
        for (auto* e : embeds)
        {
            const auto w = (::Window) e->getHostWindowID();
            if (w == 0) continue;
            now.push_back (w);
            XWindowAttributes a {};
            if (! XGetWindowAttributes (display, w, &a) || e->getWidth() <= 0 || e->getHeight() <= 0) continue;
            // The part inside the clip, in the window's own pixels.
            const auto inside = e->getLocalArea (&clip, clip.getLocalBounds()).getIntersection (e->getLocalBounds());
            if (inside == e->getLocalBounds()) { whole (w); continue; }
            const double kx = (double) a.width / e->getWidth(), ky = (double) a.height / e->getHeight();
            XRectangle r { (short) juce::roundToInt (inside.getX() * kx), (short) juce::roundToInt (inside.getY() * ky),
                           (unsigned short) juce::roundToInt (inside.getWidth() * kx), (unsigned short) juce::roundToInt (inside.getHeight() * ky) };
            XShapeCombineRectangles (display, w, ShapeBounding, 0, 0, &r, inside.isEmpty() ? 0 : 1, ShapeSet, Unsorted);
        }
        for (auto w : shaped)
            if (std::find (now.begin(), now.end(), w) == now.end()) whole (w);
        shaped = std::move (now);
        XSync (display, False);
    }

    void release()
    {
        if (display != nullptr && ! shaped.empty())
        {
            keepXErrorsQuiet();
            for (auto w : shaped) whole (w);
            XSync (display, False);
        }
        shaped.clear();
    }

    void timerCallback() override { apply(); }
};

PluginClip::PluginClip() : native (std::make_unique<Native> (*this)) { setInterceptsMouseClicks (false, true); }
PluginClip::~PluginClip() { setContent (nullptr); }

void PluginClip::setContent (juce::Component* c)
{
    if (c == content) return;
    native->release();
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
    native->apply();
}

void PluginClip::resized() { native->apply(); }

std::shared_ptr<void> watchSidewaysSwipes (juce::Component&, std::function<bool (float)>) { return {}; }
} // namespace kal
