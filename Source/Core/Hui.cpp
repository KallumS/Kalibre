#include "Hui.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace kal::hui
{

Event Decoder::feed (const std::uint8_t* b)
{
    Event e;
    const int status = b[0] & 0xF0;
    if (status == 0x90 && b[1] == 0 && b[2] == 0x7F) { e.kind = Kind::Ping; return e; }
    if (status != 0xB0) return e;
    const int cc = b[1], v = b[2];
    if (cc <= 0x07) { high[(size_t) cc] = v; return e; }
    if (cc >= 0x20 && cc <= 0x27)
    {
        e.kind = Kind::Fader;
        e.strip = cc - 0x20;
        e.value = (high[(size_t) e.strip] << 7) | v;
        return e;
    }
    if (cc == 0x0F) { zone = v; return e; }
    if (cc == 0x2F && zone >= 0)
    {
        e.zone = zone;
        e.port = v & 0x0F;
        e.pressed = (v & 0x40) != 0;
        if (zone < 8 && e.port == port::touch) { e.kind = Kind::Touch; e.strip = zone; }
        else e.kind = Kind::Switch;
        if (zone < 8) e.strip = zone;
        return e;
    }
    if (cc >= 0x40 && cc <= 0x47)
    {
        e.kind = Kind::VPot;
        e.strip = cc - 0x40;
        e.delta = (v & 0x40) ? (v & 0x3F) : -(v & 0x3F);
        return e;
    }
    return e;
}

Bytes ping() { return { 0x90, 0x00, 0x00 }; }

std::vector<Bytes> fader (int strip, double normal)
{
    const int v = (int) std::lround (std::clamp (normal, 0.0, 1.0) * 16383.0);
    return { Bytes { 0xB0, (std::uint8_t) (strip & 7), (std::uint8_t) (v >> 7) },
             Bytes { 0xB0, (std::uint8_t) (0x20 + (strip & 7)), (std::uint8_t) (v & 0x7F) } };
}

std::vector<Bytes> light (int z, int p, bool on)
{
    return { Bytes { 0xB0, 0x0C, (std::uint8_t) (z & 0x7F) }, Bytes { 0xB0, 0x2C, (std::uint8_t) ((p & 0x0F) | (on ? 0x40 : 0)) } };
}

Bytes vpotRing (int strip, double pan)
{
    // Eleven lights: 1 at the left, 6 in the middle, 11 at the right.
    const int lit = 1 + (int) std::lround ((std::clamp (pan, -1.0, 1.0) + 1.0) * 5.0);
    return { 0xB0, (std::uint8_t) (0x10 + (strip & 7)), (std::uint8_t) lit };
}

Bytes meter (int strip, int side, double peak)
{
    // Twelve steps, the top one at 0 dBFS.
    const double db = peak > 0 ? 20.0 * std::log10 (peak) : -100.0;
    const int level = std::clamp ((int) std::lround ((db + 60.0) / 60.0 * 12.0), 0, 12);
    return { 0xA0, (std::uint8_t) (strip & 7), (std::uint8_t) (((side & 1) << 4) | level) };
}

std::string fit4 (const std::string& name)
{
    std::string out;
    for (char ch : name) if (std::isalnum ((unsigned char) ch)) out += ch;
    if (out.size() > 4)
    {
        // Keep the start and any digits at the end ("Violins 2" -> "Vio2").
        std::string digits;
        while (! out.empty() && std::isdigit ((unsigned char) out.back())) { digits.insert (digits.begin(), out.back()); out.pop_back(); }
        out = out.substr (0, 4 - std::min<std::size_t> (digits.size(), 2)) + digits.substr (0, 2);
    }
    while (out.size() < 4) out += ' ';
    return out.substr (0, 4);
}

std::vector<std::uint8_t> scribble (int strip, const std::string& text)
{
    std::vector<std::uint8_t> out { 0xF0, 0x00, 0x00, 0x66, 0x05, 0x00, 0x10, (std::uint8_t) (strip & 7) };
    for (char ch : fit4 (text)) out.push_back ((std::uint8_t) (ch & 0x7F));
    out.push_back (0xF7);
    return out;
}

} // namespace kal::hui
