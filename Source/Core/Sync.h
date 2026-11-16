/*
    Sync - keeping time with other gear (decision 0034).

      - MIDI clock out: 24 pulses a quarter note, with Start, Stop, Continue
        and Song Position, so drum machines and synths follow Kalibre.
      - MIDI clock in: Kalibre follows another machine's tempo, start, stop
        and song position.
      - MIDI timecode (MTC) out: quarter-frame messages, for video and tape.
      - Ableton Link (in the engine): tempo, start and stop shared over the
        network with Live, apps and other computers.

    The arithmetic is here, with no JUCE: which clock pulses fall in a block,
    what the timecode messages say, and how fast a clock coming in is going.
*/

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace kal::sync
{

inline constexpr int pulsesPerQuarter = 24;

// The clock pulses in [fromPpq, toPpq): the first pulse's number and how many.
struct Pulses { std::int64_t first = 0; int count = 0; };
Pulses clockPulses (double fromPpq, double toPpq);

// Song Position Pointer: sixteenth notes from the start, 0-16383.
int songPosition (double ppq);
inline double ppqOfSongPosition (int sixteenths) { return sixteenths / 4.0; }

// MTC. Rates: 0 = 24, 1 = 25, 2 = 29.97 drop, 3 = 30 frames a second.
inline double framesPerSecond (int rate) { return rate == 0 ? 24.0 : rate == 1 ? 25.0 : rate == 2 ? 29.97 : 30.0; }

struct Timecode { int hours = 0, minutes = 0, seconds = 0, frames = 0; };
Timecode timecodeAt (double seconds, int rate);

// The data byte of quarter-frame message `index` (counting from 0 at 0 s).
// Eight messages carry one timecode, which describes the frame the eight began on.
std::uint8_t quarterFrame (std::int64_t index, int rate);
// When quarter frame `index` falls, in seconds.
inline double quarterFrameTime (std::int64_t index, int rate) { return (double) index / (4.0 * framesPerSecond (rate)); }

// A full-frame message (a locate): F0 7F 7F 01 01 hh mm ss ff F7.
std::array<std::uint8_t, 10> fullFrame (double seconds, int rate);

// Following a clock: the tempo from the time between its pulses.
class ClockFollower
{
public:
    void pulse (double seconds);   // a 0xF8 arrived at this time
    void reset();
    double bpm() const { return tempo; }
    bool steady() const { return count >= pulsesPerQuarter; }
    std::int64_t pulses() const { return total; }   // since the last Start, for position

    void start() { total = 0; }
    void songPosition (int sixteenths) { total = (std::int64_t) sixteenths * (pulsesPerQuarter / 4); }

private:
    std::array<double, pulsesPerQuarter> intervals {};
    int count = 0;
    double last = -1, tempo = 0;
    std::int64_t total = 0;
};

//==============================================================================
// MIDI timecode in (decision 0043): the eight quarter frames put back together.
class MtcReader
{
public:
    // A quarter frame's data byte. True when a whole timecode has just come in.
    bool quarterFrame (std::uint8_t data);
    // A full-frame message's bytes (F0 7F 7F 01 01 hh mm ss ff F7).
    bool fullFrame (const std::uint8_t* bytes, int size);
    double seconds() const { return position; }   // where the sender is now
    int rate() const { return frameRate; }
    void reset() { have = 0; }

private:
    std::array<std::uint8_t, 8> pieces {};
    int have = 0;          // bits: which pieces have come
    int frameRate = 1;
    double position = 0;
};

// MIDI Machine Control: transport commands for and from other machines.
namespace mmc
{
    enum Command { Stop = 0x01, Play = 0x02, DeferredPlay = 0x03, FastForward = 0x04, Rewind = 0x05,
                   RecordStrobe = 0x06, RecordExit = 0x07, Pause = 0x09, Locate = 0x44 };
    std::vector<std::uint8_t> command (Command, int device = 0x7F);
    std::vector<std::uint8_t> locate (double seconds, int rate, int device = 0x7F);
    struct Message { int command = 0; double seconds = 0; };
    // A sysex's bytes (with F0 and F7): an MMC message, or command 0 for anything else.
    Message decode (const std::uint8_t* bytes, int size);
}

} // namespace kal::sync
