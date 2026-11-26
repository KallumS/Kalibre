// Windows' reading of the screen for a plug-in's picture (decision 0138): the
// window asked to draw itself, child windows and DirectX included
// (PrintWindow, PW_RENDERFULLCONTENT), or the screen where it's shown.

#include "PluginPictures.h"

#include <windows.h>

#ifndef PW_RENDERFULLCONTENT
 #define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace kal::pictures
{
using namespace juce;

namespace
{
// A bitmap's pixels as an Image: Windows keeps them blue, green, red.
Image fromBitmap (HDC dc, HBITMAP bmp, int w, int h)
{
    BITMAPINFO bi {};
    bi.bmiHeader.biSize = sizeof (BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;   // top row first
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    std::vector<uint8> bits ((size_t) w * (size_t) h * 4);
    if (GetDIBits (dc, bmp, 0, (UINT) h, bits.data(), &bi, DIB_RGB_COLORS) == 0) return {};
    Image out (Image::RGB, w, h, false);
    Image::BitmapData px (out, Image::BitmapData::writeOnly);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const auto* p = bits.data() + ((size_t) y * (size_t) w + (size_t) x) * 4;
            px.setPixelColour (x, y, Colour (p[2], p[1], p[0]));
        }
    return out;
}

Image capture (HWND hwnd, Rectangle<int> area, bool fromScreen)
{
    HDC windowDc = GetDC (fromScreen ? nullptr : hwnd);
    if (windowDc == nullptr) return {};
    RECT client {};
    GetClientRect (hwnd, &client);
    const int cw = client.right - client.left, ch = client.bottom - client.top;
    HDC mem = CreateCompatibleDC (windowDc);
    HBITMAP bmp = CreateCompatibleBitmap (windowDc, cw, ch);
    auto old = SelectObject (mem, bmp);
    bool ok = false;
    if (fromScreen)
    {
        POINT origin { 0, 0 };
        ClientToScreen (hwnd, &origin);
        ok = BitBlt (mem, 0, 0, cw, ch, windowDc, origin.x, origin.y, SRCCOPY) != 0;
    }
    else ok = PrintWindow (hwnd, mem, PW_CLIENTONLY | PW_RENDERFULLCONTENT) != 0;
    SelectObject (mem, old);
    Image whole = ok ? fromBitmap (mem, bmp, cw, ch) : Image();
    DeleteObject (bmp);
    DeleteDC (mem);
    ReleaseDC (fromScreen ? nullptr : hwnd, windowDc);
    if (whole.isNull()) return {};
    area = area.getIntersection (whole.getBounds());
    return area.isEmpty() ? Image() : whole.getClippedImage (area).createCopy();
}
} // namespace

Image grabNative (Component& c)
{
    auto* peer = c.getPeer();
    if (peer == nullptr || ! c.isShowing()) return {};
    auto hwnd = (HWND) peer->getNativeHandle();
    if (hwnd == nullptr) return {};
    RECT client {};
    GetClientRect (hwnd, &client);
    auto& top = peer->getComponent();
    // Where `c` is in the window, in the window's own pixels.
    const double ratio = top.getWidth() > 0 ? (double) (client.right - client.left) / (double) top.getWidth() : 1.0;
    const auto a = top.getLocalArea (&c, c.getLocalBounds());
    const Rectangle<int> area (roundToInt (a.getX() * ratio), roundToInt (a.getY() * ratio), roundToInt (a.getWidth() * ratio), roundToInt (a.getHeight() * ratio));
    auto img = capture (hwnd, area, false);
    if (img.isNull() || looksBlank (img)) img = capture (hwnd, area, true);   // the window is in front of everything
    return img;
}

} // namespace kal::pictures
