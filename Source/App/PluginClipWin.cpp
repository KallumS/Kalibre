// The plug-in's window in the rack on Windows (decisions 0058, 0129).
//
// A plug-in's own window is a child window of Kalibre's: Windows draws it
// over whatever Kalibre draws, and JUCE can neither clip nor scale it. So
// here, as the Mac clips its native views:
// - Kalibre's own devices (drawn by JUCE) are scaled with a transform, as on Linux;
// - every child window inside the content (a VST3's or a CLAP's) is given a
//   window region: only the part inside this clip is shown, never over the
//   rack's edge, its scroll bar or the transport. Checked as it's placed and
//   ten times a second, for plug-ins that open or move their windows later.
// Shrinking a plug-in's own drawing is asked of the plug-in (VST3's content
// scale, in DeviceRack); one that can't is shown in part, clipped.
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "PluginClip.h"

#include <algorithm>
#include <vector>

namespace kal
{
struct PluginClip::Native : private juce::Timer
{
    explicit Native (PluginClip& c) : clip (c) { startTimerHz (10); }
    ~Native() override { stopTimer(); release(); }

    PluginClip& clip;
    std::vector<HWND> clipped;   // windows given a region, to give back when they leave

    // The windows directly inside Kalibre's window whose top left is in the content's area.
    std::vector<HWND> windowsInContent (HWND top, RECT area)
    {
        std::vector<HWND> found;
        for (HWND h = GetWindow (top, GW_CHILD); h != nullptr; h = GetWindow (h, GW_HWNDNEXT))
        {
            if (! IsWindowVisible (h)) continue;
            RECT r {};
            GetWindowRect (h, &r);
            MapWindowPoints (HWND_DESKTOP, top, (POINT*) &r, 2);
            const POINT corner { r.left, r.top };
            if (PtInRect (&area, corner)) found.push_back (h);
        }
        return found;
    }

    void apply()
    {
        auto* content = clip.getContent();
        auto* peer = clip.getPeer();
        if (content == nullptr || peer == nullptr || ! clip.isShowing()) { release(); return; }
        HWND top = (HWND) peer->getNativeHandle();
        const float k = (float) peer->getPlatformScaleFactor();
        auto toRect = [k] (juce::Rectangle<int> a)
        {
            const auto p = (a.toFloat() * k).getSmallestIntegerContainer();
            return RECT { p.getX(), p.getY(), p.getRight(), p.getBottom() };
        };
        const RECT shown = toRect (peer->getAreaCoveredBy (clip));
        // Where the content would be, unclipped: its windows start in there.
        const RECT area = toRect (peer->getAreaCoveredBy (*content).getUnion (peer->getAreaCoveredBy (clip)));
        auto now = windowsInContent (top, area);
        for (HWND h : clipped)
            if (std::find (now.begin(), now.end(), h) == now.end() && IsWindow (h)) SetWindowRgn (h, nullptr, TRUE);
        for (HWND h : now)
        {
            RECT r {};
            GetWindowRect (h, &r);
            MapWindowPoints (HWND_DESKTOP, top, (POINT*) &r, 2);
            RECT inside {};
            IntersectRect (&inside, &r, &shown);
            if (EqualRect (&inside, &r)) { SetWindowRgn (h, nullptr, TRUE); continue; }   // all of it shows
            // The region is in the window's own pixels, from its top left.
            HRGN region = CreateRectRgn (inside.left - r.left, inside.top - r.top, inside.right - r.left, inside.bottom - r.top);
            SetWindowRgn (h, region, TRUE);   // Windows owns the region now
        }
        clipped = std::move (now);
    }

    void release()
    {
        for (HWND h : clipped) if (IsWindow (h)) SetWindowRgn (h, nullptr, TRUE);
        clipped.clear();
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
