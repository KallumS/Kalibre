/*
    Mackie - the Mackie Control protocol (decision 0035), spoken by Mackie's
    own surfaces and by most others in their "MCU" mode (Behringer X-Touch,
    iCON, PreSonus FaderPort 8/16, Softube Console 1 Fader...).

    Eight strips, each a motorised fader, a rotary knob with a ring of lights
    (the V-Pot), Record, Solo, Mute and Select buttons, and seven letters of
    the two-line display; a master fader; transport buttons; bank buttons to
    move the eight strips along the tracks.

    This is only the protocol, with no JUCE: what a message from the surface
    means, and the bytes that move its faders, light its buttons and write on
    its display.
*/

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace kal::mackie
{

using Bytes = std::array<std::uint8_t, 3>;

namespace note
{
inline constexpr int recArm = 0, solo = 8, mute = 16, select = 24, vpotPush = 32;
inline constexpr int bankLeft = 46, bankRight = 47, channelLeft = 48, channelRight = 49;
inline constexpr int save = 80, undo = 81, marker = 84, cycle = 86;
inline constexpr int rewind = 91, forward = 92, stop = 93, play = 94, record = 95;
inline constexpr int up = 96, down = 97, left = 98, right = 99, zoom = 100, scrub = 101;
inline constexpr int faderTouch = 104;   // 104-111 the strips, 112 the master
} // namespace note

enum class Kind { None, Fader, FaderTouch, VPot, Button, Jog };

struct Input
{
    Kind kind = Kind::None;
    int strip = -1;        // 0-7, 8 for the master fader
    int button = -1;       // the note number
    bool pressed = false;
    double value = 0;      // a fader, 0-1
    int delta = 0;         // a V-Pot or the jog wheel, clicks (negative is to the left)
};

Input decode (const std::uint8_t bytes[3]);

// A fader's position, 0-1, on the same scale as volume automation (-60 to +6 dB).
Bytes fader (int strip, double position);
inline double faderToDb (double position) { return position * 66.0 - 60.0; }
inline double dbToFader (double db) { return db <= -60.0 ? 0.0 : (db + 60.0) / 66.0 > 1.0 ? 1.0 : (db + 60.0) / 66.0; }

Bytes led (int note, bool on);
// The V-Pot's ring showing pan: one light, from far left to far right.
Bytes panRing (int strip, double pan);
// A strip's meter, from its peak in dB.
Bytes meter (int strip, double peakDb);
// Writing on the display: 56 characters a line, two lines; offset 0 is the top left.
std::vector<std::uint8_t> display (int offset, const std::string& text);
// A strip's seven letters: a name squeezed to fit.
std::string fit (const std::string& name, size_t width = 7);
// The timecode display's ten digits, as controller messages, from the right.
std::vector<Bytes> timecode (const std::string& digits);

} // namespace kal::mackie
