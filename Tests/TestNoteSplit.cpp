// The Note Splitter (decision 0083): a chord's notes, top to bottom, one to each output.
#include "Check.h"
#include "NoteSplit.h"

#include <map>

using namespace kal;

namespace
{
struct Rig
{
    NoteSplit s;
    std::vector<NoteSplit::Message> out;
    Rig (int outputs) { s.setOutputs (outputs); out.reserve (512); }
    void on (int pitch, int sample = 0, double seconds = 0, int vel = 100) { std::uint8_t b[] { 0x90, (std::uint8_t) pitch, (std::uint8_t) vel }; s.add (b, 3, sample, seconds, out); }
    void off (int pitch, int sample = 0, double seconds = 0) { std::uint8_t b[] { 0x80, (std::uint8_t) pitch, 0 }; s.add (b, 3, sample, seconds, out); }
    // Which output each pitch is sounding on, from what was sent.
    std::map<int, int> sounding() const
    {
        std::map<int, int> at;
        for (auto& m : out)
        {
            if ((m.bytes[0] & 0xF0) == 0x90 && m.bytes[2] > 0) at[m.bytes[1]] = m.output;
            else if ((m.bytes[0] & 0xF0) == 0x80 && at.count (m.bytes[1]) && at[m.bytes[1]] == m.output) at.erase (m.bytes[1]);
        }
        return at;
    }
};
} // namespace

TEST ("note splitter: a four-note chord, top note to output 1 down to the bottom note to output 4")
{
    Rig r (4);
    // In any order within the moment.
    for (int p : { 48, 64, 55, 60 }) r.on (p);
    r.s.flush (r.out);
    const auto at = r.sounding();
    CHECK_EQ (at.at (64), 0);
    CHECK_EQ (at.at (60), 1);
    CHECK_EQ (at.at (55), 2);
    CHECK_EQ (at.at (48), 3);
    CHECK_EQ (r.out.size(), size_t (4));   // each started once, nowhere else
    // Each ends where it started.
    r.out.clear();
    r.off (60, 10, 0.5);
    CHECK_EQ (r.out.size(), size_t (1));
    CHECK_EQ (r.out[0].output, 1);
    CHECK_EQ ((int) (r.out[0].bytes[0] & 0xF0), 0x80);
}

TEST ("note splitter: fewer outputs than notes puts the lower ones on the last; fewer notes fill from the top")
{
    Rig r (2);
    for (int p : { 60, 64, 67 }) r.on (p);
    r.s.flush (r.out);
    auto at = r.sounding();
    CHECK_EQ (at.at (67), 0);
    CHECK_EQ (at.at (64), 1);
    CHECK_EQ (at.at (60), 1);
    Rig two (4);
    for (int p : { 50, 62 }) two.on (p);
    two.s.flush (two.out);
    at = two.sounding();
    CHECK_EQ (at.at (62), 0);
    CHECK_EQ (at.at (50), 1);
}

TEST ("note splitter: a chord played by hand, bottom note first, still ends top to bottom; a held note stays put")
{
    Rig r (3);
    r.on (48, 0, 0.000); r.s.flush (r.out);
    r.on (55, 100, 0.008); r.s.flush (r.out);
    r.on (64, 300, 0.015); r.s.flush (r.out);
    auto at = r.sounding();
    CHECK_EQ (at.at (64), 0);
    CHECK_EQ (at.at (55), 1);
    CHECK_EQ (at.at (48), 2);
    // Later, a new chord over the held bass: ranked on its own.
    r.out.clear();
    r.off (55, 0, 1.0);
    r.off (64, 0, 1.0);
    r.on (72, 10, 1.2);
    r.on (67, 10, 1.2);
    r.s.flush (r.out);
    at = r.sounding();
    CHECK_EQ (at.at (72), 0);
    CHECK_EQ (at.at (67), 1);
    // The bass, held from before, was never moved or ended.
    for (auto& m : r.out) CHECK (m.bytes[1] != 48);
}

TEST ("note splitter: controllers and the pedal go to every output; a stop ends every note")
{
    Rig r (3);
    std::uint8_t pedal[] { 0xB0, 64, 127 };
    r.s.add (pedal, 3, 0, 0, r.out);
    CHECK_EQ (r.out.size(), size_t (3));
    r.out.clear();
    for (int p : { 60, 64 }) r.on (p, 5, 0.1);
    r.s.flush (r.out);
    r.out.clear();
    r.s.allOff (0, r.out);
    CHECK_EQ (r.out.size(), size_t (2));
    for (auto& m : r.out) CHECK_EQ ((int) (m.bytes[0] & 0xF0), 0x80);
}
