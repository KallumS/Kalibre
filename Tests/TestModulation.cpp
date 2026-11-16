#include "Check.h"
#include "Modulation.h"

#include <cmath>

using namespace kal;
using namespace kal::modulation;

TEST ("lfo: shapes at known phases")
{
    CHECK (std::abs (lfoShape (LfoShape::Sine, 0.25) - 1.0) < 1e-9);
    CHECK (std::abs (lfoShape (LfoShape::Triangle, 0.5)) < 1e-9);
    CHECK_EQ (lfoShape (LfoShape::Square, 0.75), -1.0);
    CHECK (std::abs (lfoShape (LfoShape::SawUp, 0.75) - 0.5) < 1e-9);
}

TEST ("lfo: synced to the song gives the same value at the same place")
{
    Modulator m;
    m.kind = ModKind::Lfo;
    m.sync = true;
    m.syncBeats = 4;
    m.shape = LfoShape::SawUp;
    Runner r;
    r.prepare (48000);
    Clock c { 48000, 512, true, 2.0, 120 };
    const double a = r.advance (m, c, 0);
    Runner r2;
    r2.prepare (48000);
    CHECK_EQ (r2.advance (m, c, 0), a);
    CHECK (std::abs (a - 0.0) < 1e-9);   // halfway through a 4-beat cycle
}

TEST ("adsr: rises on a note, holds at sustain, falls after release")
{
    Modulator m;
    m.kind = ModKind::Adsr;
    m.attack = 0.01; m.decay = 0.01; m.sustain = 0.5; m.release = 0.01;
    Runner r;
    r.prepare (48000);
    Clock c { 48000, 4800, false, 0, 120 };   // 0.1 s blocks
    CHECK_EQ (r.advance (m, c, 0), 0.0);
    r.noteOn();
    CHECK (std::abs (r.advance (m, c, 0) - 0.5) < 1e-9);
    CHECK (std::abs (r.advance (m, c, 0) - 0.5) < 1e-9);
    r.noteOff();
    CHECK_EQ (r.advance (m, c, 0), 0.0);
}

TEST ("adr: falls to nothing without waiting for the note to end")
{
    Modulator m;
    m.kind = ModKind::Adr;
    m.attack = 0.01; m.decay = 0.05;
    Runner r;
    r.prepare (48000);
    r.noteOn();
    Clock c { 48000, 4800, false, 0, 120 };
    CHECK_EQ (r.advance (m, c, 0), 0.0);
}

TEST ("follower: follows a level up and back down")
{
    Modulator m;
    m.kind = ModKind::EnvelopeFollower;
    m.followAttack = 0.001; m.followRelease = 0.01;
    Runner r;
    r.prepare (48000);
    Clock c { 48000, 4800, false, 0, 120 };
    CHECK (r.advance (m, c, 0.8) > 0.79);
    CHECK (r.advance (m, c, 0.0) < 0.01);
}

TEST ("wavetable: a sample becomes a curve")
{
    std::vector<float> s (1000, 0.0f);
    for (int i = 500; i < 1000; ++i) s[static_cast<size_t> (i)] = (i % 2) ? 0.5f : -0.5f;
    const auto t = tableFromSamples (s.data(), 1000, 10);
    CHECK_EQ (t.size(), size_t (10));
    CHECK (std::abs (t[0]) < 1e-6);
    CHECK (std::abs (t[9]) > 0.99f);
    CHECK (std::abs (tableAt (t, 0.95) - t[9]) < 0.6);
}
