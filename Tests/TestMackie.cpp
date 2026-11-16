#include "Check.h"
#include "Mackie.h"

using namespace kal;

TEST ("mackie: faders, touches, knobs and buttons decode")
{
    const std::uint8_t faderTop[3] { 0xE2, 0x7F, 0x7F };
    auto in = mackie::decode (faderTop);
    CHECK (in.kind == mackie::Kind::Fader);
    CHECK_EQ (in.strip, 2);
    CHECK (std::abs (in.value - 1.0) < 1e-9);

    const std::uint8_t touch[3] { 0x90, 105, 127 };
    in = mackie::decode (touch);
    CHECK (in.kind == mackie::Kind::FaderTouch);
    CHECK_EQ (in.strip, 1);
    CHECK (in.pressed);

    const std::uint8_t knobLeft[3] { 0xB0, 19, 0x42 };
    in = mackie::decode (knobLeft);
    CHECK (in.kind == mackie::Kind::VPot);
    CHECK_EQ (in.strip, 3);
    CHECK_EQ (in.delta, -2);

    const std::uint8_t mute5[3] { 0x90, 20, 127 };
    in = mackie::decode (mute5);
    CHECK (in.kind == mackie::Kind::Button);
    CHECK_EQ (in.button, 20);
    CHECK_EQ (in.strip, 4);

    const std::uint8_t play[3] { 0x90, mackie::note::play, 0 };
    in = mackie::decode (play);
    CHECK (! in.pressed);
}

TEST ("mackie: what's sent back to the surface")
{
    const auto f = mackie::fader (0, mackie::dbToFader (0.0));
    CHECK_EQ (int (f[0]), 0xE0);
    CHECK (std::abs (mackie::faderToDb ((double) ((f[2] << 7) | f[1]) / 16383.0)) < 0.01);
    CHECK_EQ (int (mackie::panRing (3, -1.0)[2]), 1);
    CHECK_EQ (int (mackie::panRing (3, 0.0)[2]), 6);
    CHECK_EQ (int (mackie::panRing (3, 1.0)[1]), 51);
    CHECK_EQ (int (mackie::meter (2, 0.5)[1]), 0x2E);
    const auto lcd = mackie::display (56, "Hi");
    CHECK_EQ (lcd.size(), size_t (10));
    CHECK_EQ (int (lcd[6]), 56);
    CHECK_EQ (int (lcd.back()), 0xF7);
    CHECK_EQ (mackie::fit ("Violins 1"), std::string ("Violns1"));   // squeezed from the end, only as much as it must
    CHECK_EQ (mackie::fit ("Bass"), std::string ("Bass   "));
    const auto tc = mackie::timecode ("12");
    CHECK_EQ (tc.size(), size_t (2));
    CHECK_EQ (int (tc[0][1]), 0x40);
    CHECK_EQ (int (tc[0][2]), int ('2'));
}
