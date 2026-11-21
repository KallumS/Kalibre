#include "Check.h"
#include "DrumSounds.h"

#include <cmath>
#include <cstdio>

using namespace kal;
using drumsound::Kind;

namespace
{
library::Item sound (const std::string& path, double seconds = 0.4)
{
    library::Item it;
    it.path = path;
    it.kind = library::Kind::Audio;
    it.readable = true;
    it.seconds = seconds;
    return it;
}

// A sound as the scan hears it: its shape (0106), and its note.
library::Item heard (const std::string& path, double seconds, double onset, double attack, double tail, double zcr, int note = -1)
{
    auto it = sound (path, seconds);
    it.onsetMs = onset;
    it.attackMs = attack;
    it.tailDb = tail;
    it.zcr = zcr;
    it.note = note;
    it.analysis = library::analysisVersion;
    return it;
}
} // namespace

TEST ("drum sounds: named as a drum and short, a drum of that kind; loops, tempos, long ones and other instruments aren't (0106)")
{
    struct Case { const char* path; double seconds; bool drum; Kind kind; };
    const Case cases[] {
        { "/L/Kick 05.wav", 0.4, true, Kind::Kick },        { "/L/BD_808.wav", 0.9, true, Kind::Kick },
        { "/L/KSHMR Bass Drum 3.wav", 0.5, true, Kind::Kick }, { "/L/Snare_Tight.wav", 0.3, true, Kind::Snare },
        { "/L/HH Closed 1.wav", 0.1, true, Kind::ClosedHat }, { "/L/OHH_01.wav", 0.8, true, Kind::OpenHat },
        { "/L/Crash 1.wav", 6.0, true, Kind::Crash },        { "/L/Kick Top.wav", 0.4, true, Kind::Kick },
        { "/L/Kick Loop 01.wav", 2.0, false, Kind::Other },  { "/L/Snare 120bpm.wav", 2.0, false, Kind::Other },
        { "/L/Kick Long.wav", 12.0, false, Kind::Other },    { "/L/Bass C2.wav", 1.0, false, Kind::Other },
        { "/L/Snare Roll Riser.wav", 2.0, false, Kind::Other }, { "/L/Pad Warm.wav", 0.4, false, Kind::Other },
        { "/L/Vocal Chop.wav", 0.4, false, Kind::Other },    { "/L/Drum Fill 3.wav", 1.5, false, Kind::Other },
    };
    for (auto& c : cases)
    {
        const auto v = drumsound::of (sound (c.path, c.seconds));
        if (v.drum != c.drum || (c.drum && v.kind != c.kind)) std::printf ("    %s: drum %d kind %s\n", c.path, (int) v.drum, drumsound::nameOf (v.kind).c_str());
        CHECK (v.drum == c.drum);
        if (c.drum) { CHECK (v.kind == c.kind); CHECK (! v.heard); }
    }
    // MIDI and unreadable files aren't drums.
    auto midi = sound ("/L/Kick.mid");
    midi.kind = library::Kind::Midi;
    CHECK (! drumsound::of (midi).drum);
    // Named a kick but rising slowly (a swell): not.
    CHECK (! drumsound::of (heard ("/L/Kick Swell.wav", 1.0, 0, 300, -30, 300)).drum);
}

TEST ("drum sounds: named only by its folder, it must also be shaped like a hit; named by nothing, short and hit-shaped, its kind is heard (0106)")
{
    // In a "Snares" folder: a hit is a snare; a held sound isn't a drum.
    CHECK (drumsound::of (heard ("/L/Snares/Tight 4.wav", 0.3, 0, 2, -40, 4000)).kind == Kind::Snare);
    CHECK (! drumsound::of (heard ("/L/Snares/Ambience.wav", 0.3, 0, 2, -40, 4000)).drum);   // "Ambience": not a drum
    CHECK (! drumsound::of (heard ("/L/Snares/Held 1.wav", 1.5, 0, 2, -3, 4000)).drum);       // doesn't die away
    // In a loops folder: never.
    CHECK (! drumsound::of (heard ("/L/Drum Loops/Hit 1.wav", 0.3, 0, 2, -40, 4000)).drum);
    // Nothing named: its kind from its sound.
    struct Case { double seconds, zcr; int note; Kind kind; };
    const Case cases[] {
        { 0.4, 300, 33, Kind::Kick },        // low and dull, pitched low
        { 0.5, 1500, 50, Kind::Tom },        // pitched in the middle
        { 0.12, 14000, -1, Kind::ClosedHat },// very bright, short
        { 0.8, 14000, -1, Kind::OpenHat },
        { 1.8, 14000, -1, Kind::Cymbal },
        { 0.3, 5000, -1, Kind::Snare },      // bright
    };
    for (auto& c : cases)
    {
        const auto v = drumsound::of (heard ("/L/Misc/Thing 7.wav", c.seconds, 0, 2, -40, c.zcr, c.note));
        if (! v.drum || v.kind != c.kind) std::printf ("    zcr %.0f note %d: %s\n", c.zcr, c.note, drumsound::nameOf (v.kind).c_str());
        CHECK (v.drum);
        CHECK (v.kind == c.kind);
        CHECK (v.heard);
    }
    // Unnamed and not hit-shaped, or too long, or never heard: not a drum.
    CHECK (! drumsound::of (heard ("/L/Misc/Thing 8.wav", 0.4, 0, 150, -40, 300)).drum);
    CHECK (! drumsound::of (heard ("/L/Misc/Thing 9.wav", 3.5, 0, 2, -40, 300)).drum);
    CHECK (! drumsound::of (sound ("/L/Misc/Thing 10.wav")).drum);
}

TEST ("drum sounds: one scanned before shapes were heard is judged from its waveform's columns (0106)")
{
    auto it = sound ("/L/Misc/Hit.wav", 0.3);
    it.peaks.assign ((size_t) library::peakColumns, 0);
    for (size_t i = 0; i < it.peaks.size(); ++i) it.peaks[i] = (std::uint8_t) (255.0 * std::exp (-(double) i / 12.0));
    CHECK (drumsound::of (it).drum);
    auto pad = sound ("/L/Misc/Swell.wav", 0.3);
    pad.peaks.assign ((size_t) library::peakColumns, 200);
    CHECK (! drumsound::of (pad).drum);
}

TEST ("drum sounds: a filter's kind takes its family: Hat the closed and open, Cymbal the crash and ride (0106)")
{
    CHECK (drumsound::fits (Kind::Hat, Kind::ClosedHat));
    CHECK (drumsound::fits (Kind::Cymbal, Kind::Ride));
    CHECK (! drumsound::fits (Kind::Kick, Kind::Snare));
    CHECK_EQ (drumsound::nameOf (Kind::ClosedHat), std::string ("Closed Hat"));
}

TEST ("drum sounds: the Sounds list with Drums on shows only drums, or one kind of them; kept in the settings (0106)")
{
    library::Index ix;
    for (auto* p : { "/L/Kick 1.wav", "/L/Snare 1.wav", "/L/HH Closed.wav", "/L/Ride 2.wav", "/L/Pad Warm.wav", "/L/Kick Loop 120bpm.wav" })
        ix.items[p] = sound (p, std::string (p).find ("Loop") != std::string::npos ? 4.0 : 0.4);
    library::Settings s;
    s.folders = { "/L" };
    s.flat = true;
    auto names = [&] (drumsound::Cache* cache)
    {
        std::vector<std::string> out;
        for (auto& r : library::rows (ix, s, {}, cache)) if (r.type == library::Row::Type::Item) out.push_back (library::nameOf (r.path));
        return out;
    };
    CHECK_EQ (names (nullptr).size(), (size_t) 6);
    s.drumsOnly = true;
    drumsound::Cache cache;
    CHECK_EQ (names (&cache).size(), (size_t) 4);
    s.drumKind = (int) Kind::Hat;
    CHECK (names (&cache) == std::vector<std::string> { "HH Closed.wav" });
    s.drumKind = (int) Kind::Cymbal;
    CHECK (names (&cache) == std::vector<std::string> { "Ride 2.wav" });
    // Searched too: drums whose names hold "kick".
    s.drumKind = -1;
    std::vector<std::string> found;
    for (auto& r : library::rows (ix, s, "kick", &cache)) if (r.type == library::Row::Type::Item) found.push_back (library::nameOf (r.path));
    CHECK (found == std::vector<std::string> { "Kick 1.wav" });
    library::Settings back;
    CHECK (library::fromJson (library::toJson (s), back));
    CHECK (back.drumsOnly && back.drumKind == -1);
}
