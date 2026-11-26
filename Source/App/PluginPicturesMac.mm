// The Mac's reading of the screen for a plug-in's picture (decision 0138): the
// window server's copy of Kalibre's own window (every view in it, Metal and
// OpenGL ones too), or AppKit asked to draw the view where that gives nothing.
// CGWindowListCreateImage is looked up when it's needed: Apple has marked it
// for replacement, and a Mac without it falls back to AppKit's drawing.

#include "PluginPictures.h"

#import <AppKit/AppKit.h>
#include <dlfcn.h>

namespace kal::pictures
{
using namespace juce;

namespace
{
Image fromCG (CGImageRef cg)
{
    if (cg == nullptr) return {};
    const auto w = (int) CGImageGetWidth (cg), h = (int) CGImageGetHeight (cg);
    if (w <= 0 || h <= 0) return {};
    Image out (Image::ARGB, w, h, true);
    {
        Image::BitmapData px (out, Image::BitmapData::readWrite);
        auto space = CGColorSpaceCreateWithName (kCGColorSpaceSRGB);
        // JUCE's ARGB is premultiplied, blue first in memory.
        auto ctx = CGBitmapContextCreate (px.data, (size_t) w, (size_t) h, 8, (size_t) px.lineStride, space,
                                          (uint32_t) kCGImageAlphaPremultipliedFirst | (uint32_t) kCGBitmapByteOrder32Little);
        if (ctx != nullptr)
        {
            CGContextDrawImage (ctx, CGRectMake (0, 0, w, h), cg);
            CGContextRelease (ctx);
        }
        CGColorSpaceRelease (space);
    }
    return out;
}

using WindowListImage = CGImageRef (*) (CGRect, CGWindowListOption, CGWindowID, CGWindowImageOption);
WindowListImage windowListImage()
{
    static auto fn = (WindowListImage) dlsym (RTLD_DEFAULT, "CGWindowListCreateImage");
    return fn;
}
} // namespace

Image grabNative (Component& c)
{
    auto* peer = c.getPeer();
    if (peer == nullptr || ! c.isShowing()) return {};
    auto* view = (NSView*) peer->getNativeHandle();
    NSWindow* window = [view window];
    if (view == nil || window == nil) return {};

    // Where `c` is in the view (JUCE's views count from the top left), in points.
    auto& top = peer->getComponent();
    const double ratio = top.getWidth() > 0 ? [view bounds].size.width / (double) top.getWidth() : 1.0;
    const auto a = top.getLocalArea (&c, c.getLocalBounds());
    const NSRect inView = NSMakeRect (a.getX() * ratio, a.getY() * ratio, a.getWidth() * ratio, a.getHeight() * ratio);

    Image img;
    if (auto fn = windowListImage())
    {
        // In the screen's coordinates as the window server counts them: from the top left of the main screen.
        const NSRect onScreen = [window convertRectToScreen: [view convertRect: inView toView: nil]];
        const CGFloat mainHeight = [[[NSScreen screens] firstObject] frame].size.height;
        const CGRect r = CGRectMake (onScreen.origin.x, mainHeight - onScreen.origin.y - onScreen.size.height, onScreen.size.width, onScreen.size.height);
        if (auto cg = fn (r, kCGWindowListOptionIncludingWindow, (CGWindowID) [window windowNumber],
                          kCGWindowImageBoundsIgnoreFraming | kCGWindowImageBestResolution))
        {
            img = fromCG (cg);
            CGImageRelease (cg);
        }
    }
    if (img.isNull() || looksBlank (img))
    {
        if (NSBitmapImageRep* rep = [view bitmapImageRepForCachingDisplayInRect: inView])
        {
            [view cacheDisplayInRect: inView toBitmapImageRep: rep];
            if (auto drawn = fromCG ([rep CGImage]); drawn.isValid()) img = drawn;
        }
    }
    return img;
}

} // namespace kal::pictures
