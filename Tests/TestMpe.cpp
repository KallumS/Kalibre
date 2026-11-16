#include "Check.h"
#include "Edit.h"
#include "Mpe.h"
#include "Playback.h"

using namespace kal;

TEST ("mpe: two notes at once play on channels of their own, each with its own bend")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Seaboard");
    p.track (t)->mpe = true;
    const Id c = edit::addMidiClip (p, t, 0, PPQ * 4);
    Note a { .start = 0, .length = PPQ * 2, .pitch = 60 };
    a.expression = { { 0, 0.0, 0.2, 0.5 }, { PPQ, 2.0, 0.8, 0.7 } };   // bends up a tone after a beat
    Note b { .start = 0, .length = PPQ * 2, .pitch = 64 };
    edit::addNote (p, c, a);
    edit::addNote (p, c, b);
    const auto out = playback::compile (p, *p.track (t));
    std::vector<int> onChannels;
    int bendsOnA = 0, aChannel = -1;
    for (auto& m : out.midi)
        if ((m.bytes[0] & 0xF0) == 0x90)
        {
            onChannels.push_back (m.bytes[0] & 15);
            if (m.bytes[1] == 60) aChannel = m.bytes[0] & 15;
        }
    for (auto& m : out.midi)
        if ((m.bytes[0] & 0xF0) == 0xE0 && (m.bytes[0] & 15) == aChannel && (m.bytes[1] | (m.bytes[2] << 7)) == mpe::bendValue (2.0)) ++bendsOnA;
    CHECK_EQ (onChannels.size(), (size_t) 2);
    if (onChannels.size() == 2) CHECK (onChannels[0] != onChannels[1] && onChannels[0] != 0 && onChannels[1] != 0);
    CHECK_EQ (bendsOnA, 1);
}

TEST ("mpe: a recording's per-channel bends and pressure become each note's own")
{
    MidiData m;
    m.notes = { { .id = 1, .start = 0, .length = PPQ, .pitch = 60, .channel = 1 }, { .id = 2, .start = 0, .length = PPQ, .pitch = 67, .channel = 2 } };
    m.events = { { PPQ / 2, EventType::PitchBend, 1, 0, mpe::bendValue (1.0) },
                 { PPQ / 2, EventType::ChannelPressure, 2, 0, 127 },
                 { PPQ / 2, EventType::Controller, 0, 1, 64 } };   // the master's modulation stays an event
    mpe::gatherExpression (m);
    CHECK_EQ (m.events.size(), (size_t) 1);
    CHECK_EQ (m.notes[0].expression.size(), (size_t) 1);
    CHECK (std::abs (m.notes[0].expression[0].bend - 1.0) < 0.01);
    CHECK (std::abs (mpe::expressionAt (m.notes[1], PPQ).pressure - 1.0) < 1e-9);
}
