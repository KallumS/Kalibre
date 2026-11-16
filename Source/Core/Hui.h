/*
    Hui - the HUI protocol, for surfaces that speak it (decision 0043): older
    Mackie HUI units and many current controllers in their "HUI mode".

    Everything is on MIDI channel 1:
      - Faders: 14 bits, CC 0x00-0x07 (high) then 0x20-0x27 (low), both ways.
      - Switches and lights: a zone (CC 0x0F in, 0x0C out) then a port
        (CC 0x2F in, 0x2C out; 0x40 added when pressed or lit). Zones 0-7 are
        the strips: port 0 fader touch, 1 select, 2 mute, 3 solo, 7 record.
        Zone 0x0E is the transport: 1 rewind, 2 forward, 3 stop, 4 play,
        5 record. Zone 0x0A the banks: 0 channel left, 1 bank left,
        2 channel right, 3 bank right.
      - V-Pots: CC 0x40-0x47, relative; the rings CC 0x10-0x17.
      - Meters: polyphonic pressure, the strip as the key.
      - The strips' four-letter names: SysEx F0 00 00 66 05 00 10 ...
      - The host pings (Note On 0 0) every second, or the surface goes offline.

    No JUCE in here.
*/

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace kal::hui
{

using Bytes = std::array<std::uint8_t, 3>;

enum class Kind { None, Fader, Touch, Switch, VPot, Ping };
struct Event
{
    Kind kind = Kind::None;
    int strip = -1;         // Fader, Touch, VPot
    int value = 0;          // Fader: 0-16383
    int zone = -1, port = -1;
    bool pressed = false;   // Touch, Switch
    int delta = 0;          // VPot: clicks, + clockwise
};

namespace zone { inline constexpr int transport = 0x0E, bank = 0x0A; }
namespace port
{
    inline constexpr int touch = 0, select = 1, mute = 2, solo = 3, record = 7;
    inline constexpr int rewind = 1, forward = 2, stop = 3, play = 4, recordTransport = 5;
    inline constexpr int channelLeft = 0, bankLeft = 1, channelRight = 2, bankRight = 3;
}

class Decoder
{
public:
    Event feed (const std::uint8_t* bytes);   // one three-byte message
private:
    int zone = -1;
    std::array<int, 8> high {};
};

Bytes ping();
std::vector<Bytes> fader (int strip, double normal);
std::vector<Bytes> light (int zone, int port, bool on);
Bytes vpotRing (int strip, double pan);   // pan -1..1
Bytes meter (int strip, int side, double peak);
std::vector<std::uint8_t> scribble (int strip, const std::string& text);
std::string fit4 (const std::string&);    // a name in four letters

} // namespace kal::hui
