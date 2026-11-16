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

TEST ("playback: overlapping audio clips crossfade over the overlap, equal power")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Vox");
    Clip a;
    a.kind = Clip::Kind::Audio;
    a.start = 0;
    a.length = PPQ * 4;   // 0 - 2 s
    a.audio.file = "/a.wav";
    Clip b = a;
    b.start = PPQ * 3;    // 1.5 - 3.5 s
    b.audio.file = "/b.wav";
    edit::addClip (p, t, a);
    edit::addClip (p, t, b);
    const auto out = playback::compile (p, *p.track (t));
    CHECK (near (out.audio[0].fadeOut, 0.5));
    CHECK (near (out.audio[1].fadeIn, 0.5));
    // Halfway through, each is at -3 dB: the power adds up to one.
    const double g = playback::fadeGain (0.25, 0.5);
    CHECK (std::abs (g * g + g * g - 1.0) < 1e-9);
}

TEST ("playback: an audio clip's edges can sit between ticks, on the very sample")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Audio");
    Clip c;
    c.kind = Clip::Kind::Audio;
    c.start = PPQ;
    c.length = PPQ * 2;
    c.offset = PPQ / 2;
    c.audio.file = "/a.wav";
    c.audio.sourceLength = 10;
    c.audio.startFine = 0.0002;
    c.audio.endFine = -0.0001;
    const Id id = edit::addClip (p, t, c);
    auto out = playback::compile (p, *p.track (t));
    CHECK_EQ (out.audio.size(), size_t (1));
    const auto& r = out.audio.front();
    CHECK (near (r.startSeconds, p.tempo.secondsAt (PPQ) + 0.0002));
    CHECK (near (r.endSeconds, p.tempo.secondsAt (PPQ * 3) - 0.0001));
    CHECK (near (r.sourceSeconds, p.tempo.secondsAt (PPQ) - p.tempo.secondsAt (PPQ / 2) + 0.0002));

    // Split between ticks: the left half ends there and the right begins there, the same sample.
    const Id right = edit::splitClip (p, id, PPQ * 2, 0.00015);
    const auto* a = p.clip (id);
    const auto* b = p.clip (right);
    CHECK (near (a->audio.endFine, 0.00015));
    CHECK (near (a->audio.startFine, 0.0002));
    CHECK (near (b->audio.startFine, 0.00015));
    CHECK (near (b->audio.endFine, -0.0001));
    out = playback::compile (p, *p.track (t));
    CHECK_EQ (out.audio.size(), size_t (2));
    CHECK (near (out.audio[0].endSeconds, out.audio[1].startSeconds));
    // The right half reads the file from where the left stopped.
    CHECK (near (out.audio[1].sourceSeconds - out.audio[0].sourceSeconds, out.audio[0].endSeconds - out.audio[0].startSeconds));
}
