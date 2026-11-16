#include "Check.h"
#include "Hui.h"
#include "Sync.h"

using namespace kal;

TEST ("hui: a fader's two halves make one 14-bit value; zone then port is a switch")
{
    hui::Decoder d;
    const std::uint8_t hi[3] { 0xB0, 0x02, 0x40 }, lo[3] { 0xB0, 0x22, 0x01 };
    CHECK (d.feed (hi).kind == hui::Kind::None);
    const auto f = d.feed (lo);
    CHECK (f.kind == hui::Kind::Fader);
    CHECK_EQ (f.strip, 2);
    CHECK_EQ (f.value, (0x40 << 7) | 1);
    const std::uint8_t zone[3] { 0xB0, 0x0F, 0x03 }, press[3] { 0xB0, 0x2F, 0x40 | hui::port::mute };
    d.feed (zone);
    const auto s = d.feed (press);
    CHECK (s.kind == hui::Kind::Switch);
    CHECK_EQ (s.strip, 3);
    CHECK_EQ (s.port, hui::port::mute);
    CHECK (s.pressed);
    const std::uint8_t touch[3] { 0xB0, 0x2F, 0x40 };
    CHECK (d.feed (touch).kind == hui::Kind::Touch);
    const auto out = hui::fader (1, 1.0);
    CHECK_EQ ((int) out[0][2], 127);
    CHECK_EQ ((int) out[1][2], 127);
    CHECK_EQ (hui::fit4 ("Violins 2"), std::string ("Vio2"));
    CHECK_EQ (hui::scribble (0, "Bass").size(), (size_t) 13);
}

TEST ("sync: MTC in is put back together from eight quarter frames, two frames on")
{
    sync::MtcReader r;
    // About 01:02:03 at 25 fps: a hundred quarter frames a second, eight to a timecode.
    const std::int64_t first = (std::int64_t) (3723.5 * 100.0) / 8 * 8;
    bool done = false;
    for (int i = 0; i < 8; ++i) done = r.quarterFrame (sync::quarterFrame (first + i, 1));
    CHECK (done);
    CHECK (std::abs (r.seconds() - (first / 100.0 + 2.0 / 25.0)) < 0.021);
    CHECK_EQ (r.rate(), 1);
}

TEST ("sync: MMC commands and locates go out and come back")
{
    const auto play = sync::mmc::command (sync::mmc::Play);
    CHECK_EQ (sync::mmc::decode (play.data(), (int) play.size()).command, (int) sync::mmc::Play);
    const auto loc = sync::mmc::locate (75.0, 1);
    const auto m = sync::mmc::decode (loc.data(), (int) loc.size());
    CHECK_EQ (m.command, (int) sync::mmc::Locate);
    CHECK (std::abs (m.seconds - 75.0) < 0.001);
    const std::uint8_t other[6] { 0xF0, 0x43, 0x10, 0x4C, 0x00, 0xF7 };
    CHECK_EQ (sync::mmc::decode (other, 6).command, 0);
}
