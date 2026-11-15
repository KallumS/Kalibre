#include "Check.h"
#include "Edit.h"
#include "Playback.h"

#include <cmath>

using namespace kal;

static bool near (double a, double b) { return std::abs (a - b) < 1e-9; }

TEST ("playback: notes become note-on and note-off in seconds")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Keys");
    const Id c = edit::addMidiClip (p, t, PPQ * 4, PPQ * 4);
    edit::addNote (p, c, { 0, 0, PPQ, 60, 100 });
    const auto out = playback::compile (p, *p.track (t));
    CHECK_EQ (out.midi.size(), size_t (2));
    CHECK (near (out.midi[0].seconds, 2.0));
    CHECK_EQ (int (out.midi[0].bytes[0]), 0x90);
    CHECK (near (out.midi[1].seconds, 2.5));
    CHECK_EQ (int (out.midi[1].bytes[0]), 0x80);
}

TEST ("playback: a negative track delay plays early")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Strings");
    p.track (t)->delayMs = -125;
    const Id c = edit::addMidiClip (p, t, PPQ * 4, PPQ * 4);
    edit::addNote (p, c, { 0, 0, PPQ, 60, 100 });
    const auto out = playback::compile (p, *p.track (t));
    CHECK (near (out.midi[0].seconds, 1.875));
}

TEST ("playback: articulations send their keyswitch first and add their own delay")
{
    Project p;
    ArticulationMap m;
    m.id = p.newId();
    m.name = "Strings";
    m.articulations.push_back ({ "Legato", 24, 100, -1, 0, -1, -1, -60 });
    m.articulations.push_back ({ "Staccato", 25, 100, -1, 0, -1, 2, -10 });
    p.articulationMaps.push_back (m);
    const Id t = edit::addTrack (p, TrackKind::Track, "Violins");
    p.track (t)->articulationMap = m.id;
    p.track (t)->delayMs = -100;
    const Id c = edit::addMidiClip (p, t, PPQ * 4, PPQ * 4);
    Note legato { 0, 0, PPQ, 67, 100 };
    legato.articulation = 0;
    Note stacc { 0, PPQ * 2, PPQ / 2, 69, 100 };
    stacc.articulation = 1;
    edit::addNote (p, c, legato);
    edit::addNote (p, c, stacc);
    const auto out = playback::compile (p, *p.track (t));
    // Legato: keyswitch 24 on and off, then the note, 160 ms early.
    CHECK_EQ (int (out.midi[0].bytes[1]), 24);
    CHECK (out.midi[0].seconds < 2.0 - 0.160);
    bool foundLegato = false, foundStacc = false;
    for (auto& m2 : out.midi)
    {
        if ((m2.bytes[0] & 0xF0) == 0x90 && m2.bytes[1] == 67) { CHECK (near (m2.seconds, 2.0 - 0.160)); foundLegato = true; }
        if ((m2.bytes[0] & 0xF0) == 0x90 && m2.bytes[1] == 69) { CHECK (near (m2.seconds, 3.0 - 0.110)); CHECK_EQ (int (m2.bytes[0] & 15), 2); foundStacc = true; }
    }
    CHECK (foundLegato && foundStacc);
}

TEST ("playback: looped clips repeat and notes stop at the clip's end")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Loop");
    const Id c = edit::addMidiClip (p, t, 0, PPQ * 3 + PPQ / 2);
    p.clip (c)->loopLength = PPQ;
    edit::addNote (p, c, { 0, 0, PPQ * 2, 60, 100 });
    const auto out = playback::compile (p, *p.track (t));
    CHECK_EQ (out.midi.size(), size_t (8));
    CHECK (near (out.midi.back().seconds, 1.75));
}

TEST ("playback: muted clips are silent; audio clips become regions")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Mix");
    const Id c = edit::addMidiClip (p, t, 0, PPQ);
    edit::addNote (p, c, { 0, 0, PPQ, 60, 100 });
    p.clip (c)->muted = true;
    Clip a;
    a.kind = Clip::Kind::Audio;
    a.start = PPQ * 2;
    a.length = PPQ * 2;
    a.audio.file = "/tmp/x.wav";
    a.audio.gainDb = -6;
    edit::addClip (p, t, a);
    const auto out = playback::compile (p, *p.track (t));
    CHECK (out.midi.empty());
    CHECK_EQ (out.audio.size(), size_t (1));
    CHECK (near (out.audio[0].startSeconds, 1.0));
    CHECK (std::abs (out.audio[0].gain - 0.501) < 0.001);
    CHECK_EQ (playback::firstAtOrAfter (out.midi, 0.0), size_t (0));
}
