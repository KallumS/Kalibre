#include "Sync.h"

#include <algorithm>

namespace kal::sync
{

Pulses clockPulses (double fromPpq, double toPpq)
{
    Pulses p;
    if (toPpq <= fromPpq) return p;
    p.first = (std::int64_t) std::ceil (fromPpq * pulsesPerQuarter - 1e-9);
    const auto end = (std::int64_t) std::ceil (toPpq * pulsesPerQuarter - 1e-9);
    p.count = (int) std::max<std::int64_t> (0, end - p.first);
    return p;
}

int songPosition (double ppq)
{
    return std::clamp ((int) std::floor (ppq * 4.0 + 1e-9), 0, 16383);
}

Timecode timecodeAt (double seconds, int rate)
{
    Timecode t;
    const double fps = framesPerSecond (rate);
    auto frames = (std::int64_t) std::floor (std::max (0.0, seconds) * fps + 1e-9);
    if (rate == 2)
    {
        // Drop frame: frame numbers 0 and 1 are skipped each minute except every tenth.
        const std::int64_t perTen = 17982, perMinute = 1798;
        const auto tens = frames / perTen, rem = frames % perTen;
        frames += 18 * tens + (rem > 1 ? 2 * ((rem - 2) / perMinute) : 0);
        t.frames = (int) (frames % 30);
        t.seconds = (int) ((frames / 30) % 60);
        t.minutes = (int) ((frames / 1800) % 60);
        t.hours = (int) ((frames / 108000) % 24);
        return t;
    }
    const int f = (int) std::lround (fps);
    t.frames = (int) (frames % f);
    t.seconds = (int) ((frames / f) % 60);
    t.minutes = (int) ((frames / (f * 60)) % 60);
    t.hours = (int) ((frames / (f * 3600)) % 24);
    return t;
}

std::uint8_t quarterFrame (std::int64_t index, int rate)
{
    const int piece = (int) (index % 8);
    // The eight describe the frame they started on: two frames per eight messages.
    const double startFrame = (double) (index / 8) * 2.0;
    const auto t = timecodeAt (startFrame / framesPerSecond (rate) + 1e-6, rate);
    int value = 0;
    switch (piece)
    {
        case 0: value = t.frames & 15; break;
        case 1: value = (t.frames >> 4) & 1; break;
        case 2: value = t.seconds & 15; break;
        case 3: value = (t.seconds >> 4) & 3; break;
        case 4: value = t.minutes & 15; break;
        case 5: value = (t.minutes >> 4) & 3; break;
        case 6: value = t.hours & 15; break;
        default: value = ((t.hours >> 4) & 1) | (rate << 1); break;
    }
    return (std::uint8_t) ((piece << 4) | value);
}

std::array<std::uint8_t, 10> fullFrame (double seconds, int rate)
{
    const auto t = timecodeAt (seconds, rate);
    return { 0xF0, 0x7F, 0x7F, 0x01, 0x01, (std::uint8_t) ((rate << 5) | t.hours), (std::uint8_t) t.minutes,
             (std::uint8_t) t.seconds, (std::uint8_t) t.frames, 0xF7 };
}

void ClockFollower::reset()
{
    count = 0;
    last = -1;
    tempo = 0;
    total = 0;
}

void ClockFollower::pulse (double seconds)
{
    ++total;
    if (last >= 0)
    {
        const double gap = seconds - last;
        if (gap > 0 && gap < 0.5)
        {
            intervals[(size_t) (count % pulsesPerQuarter)] = gap;
            ++count;
            const int n = std::min (count, pulsesPerQuarter);
            double sum = 0;
            for (int i = 0; i < n; ++i) sum += intervals[(size_t) i];
            const double raw = 60.0 / ((sum / n) * pulsesPerQuarter);
            // Shown to two decimals, and held still unless it really moved.
            if (tempo <= 0 || std::abs (raw - tempo) > 0.05) tempo = raw;
        }
        else count = 0;   // a pause: start averaging again
    }
    last = seconds;
}

//==============================================================================
bool MtcReader::quarterFrame (std::uint8_t data)
{
    const int piece = (data >> 4) & 7;
    pieces[(size_t) piece] = data & 0x0F;
    if (piece == 0) have = 0;
    have |= 1 << piece;
    if (piece != 7 || have != 0xFF) return false;
    const int frames = pieces[0] | (pieces[1] << 4);
    const int secs = pieces[2] | (pieces[3] << 4);
    const int mins = pieces[4] | (pieces[5] << 4);
    const int hours = pieces[6] | ((pieces[7] & 1) << 4);
    frameRate = (pieces[7] >> 1) & 3;
    const double fps = framesPerSecond (frameRate);
    // The eight began two frames ago, on the frame they describe.
    position = hours * 3600.0 + mins * 60.0 + secs + (frames + 2) / fps;
    have = 0;
    return true;
}

bool MtcReader::fullFrame (const std::uint8_t* b, int size)
{
    if (size < 10 || b[0] != 0xF0 || b[1] != 0x7F || b[3] != 0x01 || b[4] != 0x01) return false;
    frameRate = (b[5] >> 5) & 3;
    position = (b[5] & 0x1F) * 3600.0 + b[6] * 60.0 + b[7] + b[8] / framesPerSecond (frameRate);
    have = 0;
    return true;
}

namespace mmc
{
std::vector<std::uint8_t> command (Command c, int device)
{
    return { 0xF0, 0x7F, (std::uint8_t) (device & 0x7F), 0x06, (std::uint8_t) c, 0xF7 };
}

std::vector<std::uint8_t> locate (double seconds, int rate, int device)
{
    const auto t = timecodeAt (seconds, rate);
    return { 0xF0, 0x7F, (std::uint8_t) (device & 0x7F), 0x06, 0x44, 0x06, 0x01,
             (std::uint8_t) ((rate << 5) | (t.hours & 0x1F)), (std::uint8_t) t.minutes, (std::uint8_t) t.seconds, (std::uint8_t) t.frames, 0x00, 0xF7 };
}

Message decode (const std::uint8_t* b, int size)
{
    Message m;
    if (size < 6 || b[0] != 0xF0 || b[1] != 0x7F || b[3] != 0x06) return m;
    m.command = b[4];
    if (m.command == Locate && size >= 13 && b[5] == 0x06 && b[6] == 0x01)
    {
        const int rate = (b[7] >> 5) & 3;
        m.seconds = (b[7] & 0x1F) * 3600.0 + b[8] * 60.0 + b[9] + b[10] / framesPerSecond (rate);
    }
    return m;
}
} // namespace mmc

} // namespace kal::sync
