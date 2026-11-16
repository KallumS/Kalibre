#include "Check.h"
#include "Sync.h"

using namespace kal;

TEST ("sync: clock pulses in a block, 24 a quarter note")
{
    const auto a = sync::clockPulses (0.0, 1.0);
    CHECK_EQ (a.first, std::int64_t (0));
    CHECK_EQ (a.count, 24);
    const auto b = sync::clockPulses (0.99, 1.05);   // 23.76 to 25.2: pulses 24 and 25
    CHECK_EQ (b.first, std::int64_t (24));
    CHECK_EQ (b.count, 2);
    CHECK_EQ (sync::clockPulses (2.0, 2.0).count, 0);
}

TEST ("sync: song position is in sixteenths")
{
    CHECK_EQ (sync::songPosition (4.0), 16);
    CHECK_EQ (sync::songPosition (4.24), 16);
    CHECK_EQ (sync::songPosition (-1.0), 0);
    CHECK (std::abs (sync::ppqOfSongPosition (16) - 4.0) < 1e-12);
}

TEST ("sync: timecode, including drop frame")
{
    auto t = sync::timecodeAt (3661.5, 1);   // 1:01:01 and half a second at 25
    CHECK_EQ (t.hours, 1);
    CHECK_EQ (t.minutes, 1);
    CHECK_EQ (t.seconds, 1);
    CHECK_EQ (t.frames, 12);
    // 29.97 drop: one minute in, frames 0 and 1 are skipped.
    t = sync::timecodeAt (1800.0 / 29.97, 2);
    CHECK_EQ (t.minutes, 1);
    CHECK_EQ (t.frames, 2);
}

TEST ("sync: MTC quarter frames carry the timecode in eight pieces")
{
    // At 25 fps, message 8 starts the second eight, describing frame 2.
    CHECK_EQ (int (sync::quarterFrame (8, 1)), 0x02);
    CHECK_EQ (int (sync::quarterFrame (9, 1)), 0x10);
    CHECK_EQ (int (sync::quarterFrame (15, 1)), 0x70 | (1 << 1));   // the rate in the last piece
    CHECK (std::abs (sync::quarterFrameTime (100, 1) - 1.0) < 1e-12);   // 100 quarters of a 25 fps frame = 1 s
    const auto ff = sync::fullFrame (61.0, 1);
    CHECK_EQ (int (ff[5]), 1 << 5);
    CHECK_EQ (int (ff[6]), 1);
    CHECK_EQ (int (ff[7]), 1);
}

TEST ("sync: following a clock at 128 BPM")
{
    sync::ClockFollower f;
    const double gap = 60.0 / (128.0 * 24.0);
    for (int i = 0; i < 100; ++i) f.pulse (i * gap + (i % 2 ? 0.0002 : -0.0002));   // a little jitter
    CHECK (f.steady());
    CHECK (std::abs (f.bpm() - 128.0) < 0.1);
    f.songPosition (16);
    CHECK_EQ (f.pulses(), std::int64_t (96));
}

#include "ControlSettings.h"

TEST ("sync: the studio's settings survive saving; a broken file is refused")
{
    ControlSettings s;
    s.clockOut = { "port-a", "port-b" };
    s.link = true;
    s.mackieIn = "mcu-in";
    s.oscPort = 9100;
    const auto back = loadSettings (saveSettings (s));
    CHECK (back.has_value());
    CHECK (*back == s);
    CHECK (! loadSettings ("{nope").has_value());
}
