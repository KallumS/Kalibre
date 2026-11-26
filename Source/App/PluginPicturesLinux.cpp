// Linux's reading of the screen for a plug-in's picture (decision 0138): X11's
// own, from the window itself (its plug-in's embedded windows inside it), or
// from the screen where the window gave nothing (a window drawn by OpenGL).

#include "PluginPictures.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace kal::pictures
{
using namespace juce;

namespace
{
int shiftOf (unsigned long mask)
{
    int s = 0;
    while (mask != 0 && (mask & 1) == 0) { mask >>= 1; ++s; }
    return s;
}

Image fromX (XImage* xi)
{
    if (xi == nullptr) return {};
    Image out (Image::RGB, xi->width, xi->height, false);
    const int rs = shiftOf (xi->red_mask), gs = shiftOf (xi->green_mask), bs = shiftOf (xi->blue_mask);
    const auto rm = xi->red_mask >> rs, gm = xi->green_mask >> gs, bm = xi->blue_mask >> bs;
    if (rm == 0 || gm == 0 || bm == 0) { XDestroyImage (xi); return {}; }
    Image::BitmapData px (out, Image::BitmapData::writeOnly);
    for (int y = 0; y < xi->height; ++y)
        for (int x = 0; x < xi->width; ++x)
        {
            const auto p = XGetPixel (xi, x, y);
            px.setPixelColour (x, y, Colour ((uint8) (((p >> rs) & rm) * 255 / rm), (uint8) (((p >> gs) & gm) * 255 / gm),
                                             (uint8) (((p >> bs) & bm) * 255 / bm)));
        }
    XDestroyImage (xi);
    return out;
}

int xErrors (Display*, XErrorEvent*) { return 0; }   // a window gone or off the screen: no picture, no exit
} // namespace

Image grabNative (Component& c)
{
    auto* peer = c.getPeer();
    if (peer == nullptr || ! c.isShowing()) return {};
    const auto window = (::Window) (pointer_sized_uint) peer->getNativeHandle();
    Display* d = XOpenDisplay (nullptr);
    if (d == nullptr || window == 0) { if (d) XCloseDisplay (d); return {}; }
    auto oldHandler = XSetErrorHandler (xErrors);

    Image result;
    XWindowAttributes a {};
    if (XGetWindowAttributes (d, window, &a) && a.map_state == IsViewable)
    {
        // Where `c` is in the window, in the window's own pixels.
        auto& top = peer->getComponent();
        const double ratio = top.getWidth() > 0 ? (double) a.width / (double) top.getWidth() : 1.0;
        const auto area = top.getLocalArea (&c, c.getLocalBounds());
        const auto px = Rectangle<int> (roundToInt (area.getX() * ratio), roundToInt (area.getY() * ratio),
                                        roundToInt (area.getWidth() * ratio), roundToInt (area.getHeight() * ratio))
                            .getIntersection ({ 0, 0, a.width, a.height });
        if (! px.isEmpty())
        {
            result = fromX (XGetImage (d, window, px.getX(), px.getY(), (unsigned) px.getWidth(), (unsigned) px.getHeight(), AllPlanes, ZPixmap));
            if (result.isNull() || looksBlank (result))
            {
                // From the screen instead: the window is in front of everything.
                int rx = 0, ry = 0;
                ::Window child = 0;
                const auto root = DefaultRootWindow (d);
                XWindowAttributes ra {};
                if (XTranslateCoordinates (d, window, root, px.getX(), px.getY(), &rx, &ry, &child) && XGetWindowAttributes (d, root, &ra))
                {
                    const auto onScreen = Rectangle<int> (rx, ry, px.getWidth(), px.getHeight()).getIntersection ({ 0, 0, ra.width, ra.height });
                    if (! onScreen.isEmpty())
                        if (auto fromScreen = fromX (XGetImage (d, root, onScreen.getX(), onScreen.getY(), (unsigned) onScreen.getWidth(),
                                                                (unsigned) onScreen.getHeight(), AllPlanes, ZPixmap)); fromScreen.isValid())
                            result = fromScreen;
                }
            }
        }
    }
    XSync (d, False);
    XSetErrorHandler (oldHandler);
    XCloseDisplay (d);
    return result;
}

} // namespace kal::pictures
