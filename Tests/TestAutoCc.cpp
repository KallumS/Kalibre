// AutoCC in Kalibre (decision 0060): the CC lanes AutoCC would play for a
// clip's notes, and the instrument names that let the Auto button work.
// The numbers are AutoCC's own (its README, measured on its engine).
#include "Check.h"
#include "AutoCc.h"
#include "Controllers.h"
#include "Edit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace kal;

namespace
{
// A clip at bar 1, 120 bpm (a quarter note is half a second).
struct Song
{
    Project p;
    Id track = 0, clip = 0;
    Song()
    {
        p.tempo.setTempos ({ { 0, 120.0, false } });
        track = edit::addTrack (p, TrackKind::Track, "Violins 1");
        clip = edit::addMidiClip (p, track, 0, PPQ * 32);
    }
    Clip& c() { return *p.clip (clip); }
    void note (double fromSeconds, double seconds, int velocity = 127)
    {
        Note n;
        n.start = (Tick) std::llround (fromSeconds * 2.0 * PPQ);
        n.length = (Tick) std::llround (seconds * 2.0 * PPQ);
        n.pitch = 67;
        n.velocity = velocity;
        edit::addNote (p, clip, n);
    }
};

std::vector<MidiEvent> lane (const std::vector<MidiEvent>& evs, int cc)
{
    std::vector<MidiEvent> out;
    for (auto& e : evs) if (e.type == EventType::Controller && e.number == cc) out.push_back (e);
    return out;
}

int highest (const std::vector<MidiEvent>& evs) { int v = -1; for (auto& e : evs) v = std::max (v, e.value); return v; }
int lowestAfter (const std::vector<MidiEvent>& evs, Tick from, Tick to)
{
    int v = 999;
    for (auto& e : evs) if (e.tick > from && e.tick < to) v = std::min (v, e.value);
    return v;
}
} // namespace

TEST ("autocc: the instrument in a track's name, as AutoCC's families")
{
    using autocc::Family;
    CHECK (autocc::familyFor ("Violins 1") == Family::Strings);
    CHECK (autocc::familyFor ("Vln II") == Family::Strings);
    CHECK (autocc::familyFor ("Celli") == Family::Strings);
    CHECK (autocc::familyFor ("Double Basses") == Family::Strings);
    CHECK (autocc::familyFor ("Horns a4") == Family::Brass);
    CHECK (autocc::familyFor ("Bass Trombone") == Family::Brass);
    CHECK (autocc::familyFor ("Tpt 1") == Family::Brass);
    CHECK (autocc::familyFor ("English Horn") == Family::Woodwinds);   // a woodwind, though it says horn
    CHECK (autocc::familyFor ("Bass Clarinet") == Family::Woodwinds);
    CHECK (autocc::familyFor ("Flute") == Family::Woodwinds);
    CHECK (autocc::familyFor ("Full Orchestra") == Family::Ensemble);
    CHECK (autocc::familyFor ("Synth Bass") == Family::None);
    CHECK (autocc::familyFor ("Piano") == Family::None);
    CHECK (autocc::familyFor ("Track 1") == Family::None);

    // A track named for nothing, with an instrument that is: found there.
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Track 1");
    PluginRef r;
    r.name = "BBC SO Violas";
    r.format = "VST3";
    r.identifier = "x";
    edit::addDevice (p, t, r, DeviceRole::Instrument);
    const auto found = autocc::familyFor (*p.track (t));
    CHECK (found.family == Family::Strings);
    CHECK_EQ (found.from, std::string ("BBC SO Violas"));
}

TEST ("autocc: every instrument of each section, in the ways track names write them")
{
    using autocc::Family;
    const char* strings[] { "Violin 1", "Violins 1", "Vlns one", "Vln. I", "VlnII", "1st Violins", "Violins1", "V1", "V2", "Vn 2",
                            "Viola", "Violas", "Vla", "Vlas", "Va", "Cello", "Cellos", "Celli", "Vc", "Vcs", "Vlc", "Violoncello",
                            "Double Bass", "Double Basses", "Contrabass", "Basses", "CB", "String Bass", "Strings", "Str", "Stgs",
                            "Violini I", "Viole", "Violoncelli", "Contrabbassi", "Violons", "Violoncelles", "Contrebasses",
                            "Violinen", "Geigen", "Bratschen", "Kontrab\xc3\xa4sse", "Fiddle", "Erhu", "Viola da Gamba", "Octobass",
                            "V i o l i n s  1", "Spitfire Symphonic Strings" };
    const char* brass[] { "Horn", "Horns", "French Horn", "Horn in F", "Hn 1", "Hns", "Fhn", "Horns a4", "Corni", "H\xc3\xb6rner",
                          "Trumpet", "Trumpets 1-2", "Tpt", "Tpts", "Trp", "Piccolo Trumpet", "Trombe", "Trompeten", "Trombone",
                          "Trombones", "Tbn", "Trb", "Bones", "Bass Trombone", "Tromboni", "Posaunen", "Tuba", "Tubas",
                          "Cimbasso", "Euphonium", "Flugelhorn", "Fl\xc3\xbcgelhorn", "Cornet", "Bugle", "Mellophone",
                          "Sousaphone", "Wagner Tuba", "Alto Horn", "Sackbut", "Brass", "Low Brass" };
    const char* winds[] { "Flute", "Flutes 1-3", "Fl 1", "Fls", "Alto Flute", "Bass Flute", "Piccolo", "Picc", "Ottavino", "Oboe",
                          "Oboes", "Ob", "Oboe d\'Amore", "English Horn", "Cor Anglais", "Corno Inglese", "Englischhorn", "Clarinet",
                          "Clarinets in Bb", "Cl", "Clar", "Bass Clarinet", "Basset Horn", "Bassoon", "Bassoons", "Bsn",
                          "Contrabassoon", "Contrafagotto", "Fagotti", "Fl\xc3\xb6ten", "Fl\xc3\xbbte", "Hautbois", "Klarinetten",
                          "Saxophone", "Alto Sax", "Baritone Sax", "Recorder", "Tin Whistle", "Low Whistle", "Pan Flute",
                          "Shakuhachi", "Bansuri", "Dizi", "Duduk", "Ocarina", "Bagpipes", "Woodwinds", "Winds", "WW" };
    for (auto* n : strings) { CHECK (autocc::familyFor (n) == Family::Strings); if (autocc::familyFor (n) != Family::Strings) std::printf ("    not strings: %s\n", n); }
    for (auto* n : brass) { CHECK (autocc::familyFor (n) == Family::Brass); if (autocc::familyFor (n) != Family::Brass) std::printf ("    not brass: %s\n", n); }
    for (auto* n : winds) { CHECK (autocc::familyFor (n) == Family::Woodwinds); if (autocc::familyFor (n) != Family::Woodwinds) std::printf ("    not woodwinds: %s\n", n); }
    // Every name Orchestration Helper (KallumS/Orchestration-Helper) gives these instruments.
    const char* ohStrings[] { "violin i", "violins ii", "first violins", "second violins", "vn", "vns", "fiddle", "vln", "alto violin",
                              "'cellos", "violoncellos", "double-bass", "doublebass", "d. basses", "db", "string bass",
                              "string quartet", "string orchestra", "bowed strings", "stringed instruments", "string group" };
    const char* ohWinds[] { "concert flute", "transverse flute", "piccolos", "piccolo flute", "altoflute", "flute in g", "alto fl",
                            "englishhorn", "eng horn", "eh", "alto oboe", "b flat clarinet", "a clarinet", "e flat clarinet",
                            "eb clarinet", "small clarinet", "piccolo clarinet", "d clarinet", "sopranino clarinet", "bassclarinet",
                            "bass cl", "bcl", "pedal clarinet", "fagotto", "fagott", "fag", "bn", "c-fag", "cbn", "double-bassoon",
                            "wood-wind", "wood wind", "wws", "woodwind section" };
    const char* ohBrass[] { "french horns", "waldhorn", "corno", "f horn", "cor", "tromba", "clarino", "alto trumpet", "small trumpet",
                            "bass trumpet", "cornetts", "corneta", "cornet a pistons", "posaune", "sackbut", "tenor trombone",
                            "wagner tubas", "tuben", "ophicleide", "brasswind", "brass-wind", "brass group", "horns and trumpets" };
    for (auto* n : ohStrings) { CHECK (autocc::familyFor (n) == Family::Strings); if (autocc::familyFor (n) != Family::Strings) std::printf ("    not strings: %s\n", n); }
    for (auto* n : ohWinds) { CHECK (autocc::familyFor (n) == Family::Woodwinds); if (autocc::familyFor (n) != Family::Woodwinds) std::printf ("    not woodwinds: %s\n", n); }
    for (auto* n : ohBrass) { CHECK (autocc::familyFor (n) == Family::Brass); if (autocc::familyFor (n) != Family::Brass) std::printf ("    not brass: %s\n", n); }
    // AutoCC has nothing for percussion, harp, piano or voices: those stay off, though they share words.
    for (auto* n : { "Choir Basses", "Chorus Altos", "Orchestra Bells", "Bass Drum", "Gran Cassa", "Timpani", "Tubular Bells", "Harps",
                     "Celesta", "Harpsichord", "Mezzo-Soprano", "Tenor Voice", "Glockenspiel" })
    {
        CHECK (autocc::familyFor (n) == Family::None);
        if (autocc::familyFor (n) != Family::None) std::printf ("    should be none: %s\n", n);
    }
    for (auto* n : { "Full Orchestra", "Tutti", "Orchestral Hits" }) CHECK (autocc::familyFor (n) == Family::Ensemble);
    for (auto* n : { "Synth Bass", "Bass", "Electric Bass", "Piano", "Harp", "Choir", "Pad", "Drums", "Track 1", "Vocals 1", "Lead Synth", "Wind", "Vox V2", "Altos" })
    {
        CHECK (autocc::familyFor (n) == Family::None);
        if (autocc::familyFor (n) != Family::None) std::printf ("    should be none: %s\n", n);
    }
}

TEST ("autocc: a held string note swells from 85 to 116 on CC1, holds, and falls back; CC7 and CC21 are held levels")
{
    Song s;
    s.note (1.0, 4.0);
    const auto evs = autocc::render (s.c(), s.p.tempo, autocc::Family::Strings);
    const auto cc1 = lane (evs, 1), cc11 = lane (evs, 11), cc7 = lane (evs, 7), cc21 = lane (evs, 21);
    CHECK (! cc1.empty() && cc1.front().value == 85 && cc1.front().tick == 0);   // the resting level, from the start
    CHECK_EQ (highest (cc1), 116);
    CHECK_EQ (highest (cc11), 118);
    // Swelled by 2.2 s after the note began; still there just before it ends.
    const Tick at3_3s = (Tick) (3.3 * 2 * PPQ), at4_9s = (Tick) (4.9 * 2 * PPQ);
    CHECK (lowestAfter (cc1, at3_3s, at4_9s) >= 115);
    // Back at the floor a second after the key comes up.
    CHECK_EQ (cc1.back().value, 85);
    CHECK (cc1.back().tick <= (Tick) (6.05 * 2 * PPQ));
    CHECK_EQ (cc7.size(), size_t (1));
    CHECK_EQ (cc7.front().value, 94);
    CHECK_EQ (cc21.size(), size_t (1));
    CHECK_EQ (cc21.front().value, 92);
}

TEST ("autocc: a key touched for 200 ms takes Strings CC1 only to 91 (AutoCC's README)")
{
    Song s;
    s.note (1.0, 0.2, 100);
    const auto cc1 = lane (autocc::render (s.c(), s.p.tempo, autocc::Family::Strings), 1);
    CHECK_EQ (highest (cc1), 91);
}

TEST ("autocc: a note soon after the last carries the line on; after a long break it swells again")
{
    Song s;
    s.note (0.5, 3.0);
    s.note (3.7, 2.0);    // 0.2 s after: no new swell, the lane holds where it fell to
    const auto cc1 = lane (autocc::render (s.c(), s.p.tempo, autocc::Family::Strings), 1);
    CHECK (lowestAfter (cc1, (Tick) (3.5 * 2 * PPQ), (Tick) (5.6 * 2 * PPQ)) >= 105);

    Song t;
    t.note (0.5, 3.0);
    t.note (6.0, 2.0);    // 2.5 s after: fallen to the floor, then a fresh swell
    const auto again = lane (autocc::render (t.c(), t.p.tempo, autocc::Family::Strings), 1);
    CHECK_EQ (lowestAfter (again, (Tick) (3.5 * 2 * PPQ), (Tick) (6.01 * 2 * PPQ)), 85);
}

TEST ("autocc: brass starts high and blooms; woodwinds start low")
{
    Song s;
    s.note (0.5, 3.0);
    const auto brass = lane (autocc::render (s.c(), s.p.tempo, autocc::Family::Brass), 1);
    CHECK_EQ (brass.front().value, 102);
    CHECK_EQ (highest (brass), 109);
    const auto winds = lane (autocc::render (s.c(), s.p.tempo, autocc::Family::Woodwinds), 1);
    CHECK_EQ (winds.front().value, 46);
    CHECK_EQ (highest (winds), 63);
}

TEST ("autocc: Auto fills the clip's lanes, replacing what those lanes had and nothing else, and shows them")
{
    Song s;
    s.note (0.5, 2.0);
    MidiEvent old;
    old.type = EventType::Controller;
    old.number = 1;
    old.value = 5;
    old.tick = PPQ;
    MidiEvent pan = old;
    pan.number = 10;
    s.c().midi.events = { old, pan };
    s.p.track (s.track)->ccLanes = { 64 };
    const auto written = autocc::fill (s.p, s.clip, autocc::Family::Strings);
    CHECK (written == std::vector<int> ({ 1, 11, 7, 21 }));
    const auto& evs = s.c().midi.events;
    CHECK (std::none_of (evs.begin(), evs.end(), [] (const MidiEvent& e) { return e.number == 1 && e.value == 5; }));
    CHECK (std::any_of (evs.begin(), evs.end(), [] (const MidiEvent& e) { return e.number == 10; }));   // pan kept
    CHECK (std::is_sorted (evs.begin(), evs.end(), [] (const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; }));
    const auto& lanes = s.p.track (s.track)->ccLanes;
    for (int cc : { 64, 1, 11, 7, 21 }) CHECK (std::find (lanes.begin(), lanes.end(), cc) != lanes.end());
}

TEST ("controllers: the standard names")
{
    CHECK_EQ (controllers::name (1), std::string ("Modulation"));
    CHECK_EQ (controllers::name (11), std::string ("Expression"));
    CHECK_EQ (controllers::name (64), std::string ("Sustain Pedal"));
    CHECK_EQ (controllers::name (74), std::string ("Brightness (Cutoff)"));
    CHECK_EQ (controllers::name (3), std::string ("Undefined"));
    CHECK_EQ (controllers::label (7), std::string ("CC 7 Volume"));
    // A new track shows the two every orchestral library reads.
    Track t;
    CHECK (t.ccLanes == std::vector<int> ({ 1, 11 }));
}
