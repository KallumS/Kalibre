#include "Mackie.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace kal::mackie
{

Input decode (const std::uint8_t b[3])
{
    Input in;
    const int status = b[0] & 0xF0, channel = b[0] & 0x0F;
    if (status == 0xE0)
    {
        in.kind = Kind::Fader;
        in.strip = channel;
        in.value = (double) ((b[2] << 7) | b[1]) / 16383.0;
    }
    else if (status == 0x90 || status == 0x80)
    {
        in.button = b[1];
        in.pressed = status == 0x90 && b[2] > 0;
        if (b[1] >= note::faderTouch && b[1] <= note::faderTouch + 8)
        {
            in.kind = Kind::FaderTouch;
            in.strip = b[1] - note::faderTouch;
        }
        else
        {
            in.kind = Kind::Button;
            if (b[1] < 40) in.strip = b[1] % 8;
        }
    }
    else if (status == 0xB0 && b[1] >= 16 && b[1] <= 23)
    {
        in.kind = Kind::VPot;
        in.strip = b[1] - 16;
        in.delta = (b[2] & 0x40) ? -(b[2] & 0x3F) : (b[2] & 0x3F);
    }
    else if (status == 0xB0 && b[1] == 60)
    {
        in.kind = Kind::Jog;
        in.delta = (b[2] & 0x40) ? -(b[2] & 0x3F) : (b[2] & 0x3F);
    }
    return in;
}

Bytes fader (int strip, double position)
{
    const int v = std::clamp ((int) std::lround (position * 16383.0), 0, 16383);
    return { (std::uint8_t) (0xE0 | (strip & 15)), (std::uint8_t) (v & 127), (std::uint8_t) (v >> 7) };
}

Bytes led (int n, bool on) { return { 0x90, (std::uint8_t) (n & 127), (std::uint8_t) (on ? 127 : 0) }; }

Bytes panRing (int strip, double pan)
{
    const int pos = std::clamp ((int) std::lround ((pan + 1.0) * 5.0) + 1, 1, 11);
    return { 0xB0, (std::uint8_t) (48 + (strip & 7)), (std::uint8_t) pos };   // mode 0: a single dot
}

Bytes meter (int strip, double peakDb)
{
    // 0 is silence, 12 is clipping, in steps of about 5 dB below 0 dBFS.
    int level = peakDb <= -60.0 ? 0 : std::clamp ((int) std::lround ((peakDb + 60.0) / 5.0), 0, 12);
    if (peakDb >= 0.0) level = 14;   // the overload light
    return { 0xD0, (std::uint8_t) (((strip & 7) << 4) | level), 0 };
}

std::vector<std::uint8_t> display (int offset, const std::string& text)
{
    std::vector<std::uint8_t> out { 0xF0, 0x00, 0x00, 0x66, 0x14, 0x12, (std::uint8_t) std::clamp (offset, 0, 111) };
    for (char ch : text) out.push_back ((std::uint8_t) ((ch >= 0x20 && ch < 0x7F) ? ch : '?'));
    out.push_back (0xF7);
    return out;
}

std::string fit (const std::string& name, size_t width)
{
    std::string s;
    for (char ch : name) if (ch >= 0x20 && ch < 0x7F) s += ch;
    if (s.size() > width)
    {
        // Squeeze: lose the spaces, then the lower-case vowels after the first letter.
        s.erase (std::remove (s.begin(), s.end(), ' '), s.end());
        for (size_t i = s.size(); s.size() > width && i-- > 1;)
            if (std::string ("aeiou").find (s[i]) != std::string::npos) s.erase (i, 1);
        if (s.size() > width) s.resize (width);
    }
    while (s.size() < width) s += ' ';
    return s;
}

std::vector<Bytes> timecode (const std::string& digits)
{
    std::vector<Bytes> out;
    const std::string d = digits.size() > 10 ? digits.substr (digits.size() - 10) : digits;
    for (size_t i = 0; i < d.size(); ++i)
    {
        const char ch = (char) std::toupper ((unsigned char) d[d.size() - 1 - i]);
        // The display's characters: 0x30-0x39 digits as themselves, letters 0x01-0x1A.
        const int code = (ch >= '0' && ch <= '9') ? ch : (ch >= 'A' && ch <= 'Z') ? ch - 'A' + 1 : 0x20;
        out.push_back ({ 0xB0, (std::uint8_t) (0x40 + i), (std::uint8_t) (code & 0x7F) });
    }
    return out;
}

} // namespace kal::mackie
