#include "PluginClip.h"

#include <juce_gui_extra/juce_gui_extra.h>

#import <AppKit/AppKit.h>

// A view that clips what's inside it to its own bounds, and counts from the
// top left as Kalibre's views do. Since macOS 14 a view doesn't clip what's
// inside it unless it says so, and a plug-in's layer-backed view needs its
// layer clipped too. Its bounds are its frame divided by the scale: AppKit
// then draws everything inside it, the plug-in's own view too, scaled the
// same both ways, and hands it the mouse in its own units.
@interface KalibrePluginClipView : NSView
{
@public
    CGFloat kalibreScale;
}
@end

@implementation KalibrePluginClipView
- (BOOL) isFlipped { return YES; }
- (BOOL) clipsToBounds { return YES; }
- (BOOL) isOpaque { return NO; }
- (void) kalibreApplyScale
{
    const CGFloat s = kalibreScale > 0.01 ? kalibreScale : 1.0;
    const NSSize f = [self frame].size;
    [self setBoundsOrigin: NSZeroPoint];
    [self setBoundsSize: NSMakeSize (f.width / s, f.height / s)];
    [self setNeedsDisplay: YES];
}
- (void) setFrameSize: (NSSize) size
{
    [super setFrameSize: size];
    [self kalibreApplyScale];
}
@end

namespace kal
{
struct PluginClip::Native
{
    juce::NSViewComponent holder;
};

PluginClip::PluginClip() : native (std::make_unique<Native>())
{
    setInterceptsMouseClicks (false, true);
    KalibrePluginClipView* view = [[KalibrePluginClipView alloc] initWithFrame: NSMakeRect (0, 0, 1, 1)];
    view->kalibreScale = 1.0;
    [view setWantsLayer: YES];
    view.layer.masksToBounds = YES;
    native->holder.setView (view);
    [view release];   // the holder keeps it
    addAndMakeVisible (native->holder);
}

PluginClip::~PluginClip()
{
    setContent (nullptr);
}

void PluginClip::setContent (juce::Component* c)
{
    if (c == content) return;
    if (content != nullptr && content->isOnDesktop()) content->removeFromDesktop();
    content = c;
    if (content != nullptr)
    {
        // Its own native view, inside the clipping one: the plug-in's view goes in that.
        content->addToDesktop (0, native->holder.getView());
        content->setVisible (true);
        place();
    }
}

void PluginClip::setContentPlacement (juce::Point<int> p, float s)
{
    position = p;
    scale = s;
    place();
}

// The content's position is in the clipping view's own (scaled) units.
void PluginClip::place()
{
    auto* view = (KalibrePluginClipView*) native->holder.getView();
    if (view->kalibreScale != (CGFloat) scale)
    {
        view->kalibreScale = (CGFloat) scale;
        [view kalibreApplyScale];
    }
    if (content != nullptr)
        content->setTopLeftPosition (juce::roundToInt ((float) position.x / scale), juce::roundToInt ((float) position.y / scale));
}

void PluginClip::resized()
{
    native->holder.setBounds (getLocalBounds());
    [(KalibrePluginClipView*) native->holder.getView() kalibreApplyScale];
}

//==============================================================================
namespace
{
struct SwipeWatch
{
    juce::Component::SafePointer<juce::Component> area;
    std::function<bool (float)> moved;
    id monitor = nil;
    bool decided = false, sideways = false;
    CGFloat seenX = 0, seenY = 0;

    // Is this swipe, as a whole, sideways? Decided once at its start, so a
    // swipe up the screen that drifts a little doesn't move the rack.
    bool isSideways (NSEvent* e)
    {
        const auto phase = [e phase], momentum = [e momentumPhase];
        const CGFloat dx = [e scrollingDeltaX], dy = [e scrollingDeltaY];
        if (phase == NSEventPhaseNone && momentum == NSEventPhaseNone)
            return std::abs (dx) > std::abs (dy);   // a mouse's wheel, event by event
        if ((phase & (NSEventPhaseMayBegin | NSEventPhaseBegan)) != 0) { decided = false; seenX = seenY = 0; }
        if (! decided && momentum == NSEventPhaseNone)
        {
            seenX += std::abs (dx);
            seenY += std::abs (dy);
            if (seenX + seenY < 2.0) return false;
            decided = true;
            sideways = seenX > seenY;
        }
        return decided && sideways;
    }

    NSEvent* handle (NSEvent* e)
    {
        auto* comp = area.getComponent();
        if (comp == nullptr || ! comp->isShowing() || ! moved) return e;
        auto* peer = comp->getPeer();
        if (peer == nullptr) return e;
        NSView* peerView = (NSView*) peer->getNativeHandle();
        if ([e window] == nil || [e window] != [peerView window]) return e;
        const NSPoint p = [peerView convertPoint: [e locationInWindow] fromView: nil];   // the peer's view counts from the top left
        if (! peer->getAreaCoveredBy (*comp).toFloat().contains ((float) p.x, (float) p.y)) return e;
        if (! isSideways (e)) return e;
        // Trackpads give points; a wheel gives lines.
        const float px = (float) [e scrollingDeltaX] * ([e hasPreciseScrollingDeltas] ? 1.0f : 16.0f);
        return moved (px) ? nil : e;
    }
};
} // namespace

std::shared_ptr<void> watchSidewaysSwipes (juce::Component& area, std::function<bool (float)> moved)
{
    auto* w = new SwipeWatch();
    w->area = &area;
    w->moved = std::move (moved);
    w->monitor = [NSEvent addLocalMonitorForEventsMatchingMask: NSEventMaskScrollWheel
                                                       handler: ^NSEvent* (NSEvent* e) { return w->handle (e); }];
    [w->monitor retain];
    return std::shared_ptr<void> (w, [] (void* p)
    {
        auto* sw = static_cast<SwipeWatch*> (p);
        if (sw->monitor != nil) { [NSEvent removeMonitor: sw->monitor]; [sw->monitor release]; }
        delete sw;
    });
}
} // namespace kal
