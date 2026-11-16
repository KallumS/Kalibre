#include "Check.h"
#include "Edit.h"
#include "MidiLearn.h"

using namespace kal;

TEST ("midi learn: controllers and pitch bend are learnable, notes aren't")
{
    int ch = 0, cc = 0;
    const std::uint8_t knob[3] { 0xB2, 74, 100 };
    CHECK (midimap::keyOf (knob, ch, cc));
    CHECK_EQ (ch, 3);
    CHECK_EQ (cc, 74);
    const std::uint8_t bend[3] { 0xE0, 0x7F, 0x7F };
    CHECK (midimap::keyOf (bend, ch, cc));
    CHECK_EQ (cc, midimap::pitchBend);
    CHECK (std::abs (midimap::normalised (bend) - 1.0) < 1e-9);
    const std::uint8_t note[3] { 0x90, 60, 100 };
    CHECK (! midimap::keyOf (note, ch, cc));
}

TEST ("midi learn: learning replaces, matching respects the channel")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Synth");
    AutoTarget vol;
    vol.kind = AutoTarget::Kind::Volume;
    midimap::learn (p, 1, 7, t, vol, "Synth Volume");
    AutoTarget pan;
    pan.kind = AutoTarget::Kind::Pan;
    midimap::learn (p, 1, 10, t, pan, "Synth Pan");
    midimap::learn (p, 1, 11, t, vol, "Synth Volume");   // volume moves to CC 11
    CHECK_EQ (p.midiMappings.size(), size_t (2));
    const std::uint8_t cc11[3] { 0xB0, 11, 64 };
    const std::uint8_t cc7[3] { 0xB0, 7, 64 };
    const std::uint8_t otherChannel[3] { 0xB5, 11, 64 };
    CHECK (midimap::match (p.midiMappings, cc11) >= 0);
    CHECK_EQ (midimap::match (p.midiMappings, cc7), -1);
    CHECK_EQ (midimap::match (p.midiMappings, otherChannel), -1);
    CHECK (std::abs (midimap::volumeFromNormal (60.0 / 66.0)) < 1e-9);   // 0 dB
}
