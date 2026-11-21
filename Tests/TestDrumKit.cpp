#include "Check.h"
#include "DrumKit.h"

#include <map>
#include <set>

using namespace kal;

TEST ("drums: the standard kit is eight pads on General MIDI's notes, the hats one choke group, a lane each in the pattern (0105)")
{
    const auto k = drumkit::standard();
    CHECK_EQ (k.pads.size(), (size_t) 8);
    CHECK_EQ (k.pads[0].name, std::string ("Kick"));
    CHECK_EQ (k.pads[0].note, 36);
    CHECK_EQ (k.pads[3].choke, k.pads[4].choke);
    CHECK (k.pads[3].choke > 0);
    CHECK_EQ (k.pattern.lanes.size(), k.pads.size());
    for (size_t i = 0; i < k.pads.size(); ++i)
    {
        CHECK_EQ (k.pattern.lanes[i].pitch, k.pads[i].note);
        CHECK_EQ (k.pattern.lanes[i].velocity.size(), (size_t) 16);
    }
    CHECK_EQ (drumkit::patternLength (k), PPQ * 4);
}

TEST ("drums: a kit and its pattern come back from their state, plug-ins and all; pads are added on free notes and taken out with their lanes (0105)")
{
    auto k = drumkit::standard();
    k.pads[1].sample = "/Sounds/Snares/Tight.wav";
    k.pads[2].plugin.format = "VST3";
    k.pads[2].plugin.name = "Drum Synth";
    k.pads[2].pluginState = "QUJD";
    k.pads[2].volumeDb = -4.5;
    k.pads[0].pan = 0.25;
    k.pattern.lanes[0].velocity[0] = 110;
    k.pattern.lanes[1].velocity[4] = 90;
    k.pattern.swing = 0.2;
    k.playsPattern = false;
    const auto back = drumkit::fromJson (drumkit::toJson (k));
    CHECK (back.has_value());
    if (back) CHECK (*back == k);
    CHECK (! drumkit::fromJson ("{}").has_value());

    const int added = drumkit::addPad (k);
    CHECK_EQ (added, 8);
    CHECK_EQ (k.pads[8].note, 37);   // the next free drum: the rim
    CHECK_EQ (k.pads[8].name, patterns::drumName (37));
    CHECK_EQ (k.pattern.lanes.size(), (size_t) 9);
    drumkit::removePad (k, 0);
    CHECK_EQ (k.pads[0].name, std::string ("Snare"));
    CHECK_EQ (k.pattern.lanes[0].velocity[4], 90);   // the lane went with its pad
}

TEST ("drums: a sound's kind is read from its name and folders, as sample libraries name them (0105)")
{
    using drumkit::Kind;
    const std::map<std::string, Kind> names {
        { "/S/Kick 05.wav", Kind::Kick }, { "/S/BD_808.wav", Kind::Kick }, { "/S/Kicks/Punchy 01.wav", Kind::Kick },
        { "/S/KSHMR Bass Drum 3.wav", Kind::Kick }, { "/S/Snare_Tight.wav", Kind::Snare }, { "/S/SNR 02.aif", Kind::Snare },
        { "/S/Drums/SD Crack.wav", Kind::Snare }, { "/S/Clap 2.wav", Kind::Clap }, { "/S/HH Closed 1.wav", Kind::ClosedHat },
        { "/S/Hihat Open 2.wav", Kind::OpenHat }, { "/S/OHH_01.wav", Kind::OpenHat }, { "/S/CH 3.wav", Kind::ClosedHat },
        { "/S/Hats/Tick.wav", Kind::Hat }, { "/S/Tom Low.wav", Kind::Tom }, { "/S/Crash 1.wav", Kind::Crash },
        { "/S/Ride Bell.wav", Kind::Ride }, { "/S/Rimshot.wav", Kind::Rim }, { "/S/Shaker 4.wav", Kind::Shaker },
        { "/S/Perc/Wood.wav", Kind::Percussion }, { "/S/Pads/Warm.wav", Kind::Other },
    };
    for (auto& [path, kind] : names)
    {
        if (drumkit::kindOf (path) != kind) std::printf ("    %s: %d\n", path.c_str(), (int) drumkit::kindOf (path));
        CHECK (drumkit::kindOf (path) == kind);
    }
    CHECK (drumkit::kindOf ("Closed Hat") == Kind::ClosedHat);
    CHECK (drumkit::kindOf ("Low Tom") == Kind::Tom);
}

namespace
{
library::Item sound (const std::string& path, double seconds = 0.5, bool loop = false)
{
    library::Item it;
    it.path = path;
    it.kind = library::Kind::Audio;
    it.readable = true;
    it.seconds = seconds;
    it.loop = loop;
    return it;
}
} // namespace

TEST ("drums: Random Samples gives each pad a one-shot of its kind, never a loop or a long sound, each a different one, a new choice each time (0105)")
{
    library::Index ix;
    for (auto* p : { "/S/Kicks/K1.wav", "/S/Kicks/K2.wav", "/S/Kicks/K3.wav", "/S/Snare 1.wav", "/S/Snare 2.wav", "/S/Clap.wav",
                     "/S/HH Closed.wav", "/S/HH Open.wav", "/S/Toms/Low.wav", "/S/Crash.wav", "/S/Perc/Wood.wav", "/S/Fx/Zap.wav" })
        ix.items[p] = sound (p);
    ix.items["/S/Drum Loop 120bpm.wav"] = sound ("/S/Drum Loop 120bpm.wav", 2.0, true);
    ix.items["/S/Kick Long.wav"] = sound ("/S/Kick Long.wav", 12.0);
    auto midi = sound ("/S/Kick.mid");
    midi.kind = library::Kind::Midi;
    ix.items[midi.path] = midi;

    const auto k = drumkit::standard();
    std::set<std::string> kicks;
    for (unsigned seed = 1; seed <= 20; ++seed)
    {
        const auto chosen = drumkit::randomSamples (k, ix, seed);
        CHECK_EQ (chosen.size(), k.pads.size());
        CHECK (drumkit::kindOf (chosen[0]) == drumkit::Kind::Kick);
        CHECK (drumkit::kindOf (chosen[1]) == drumkit::Kind::Snare);
        CHECK (drumkit::kindOf (chosen[2]) == drumkit::Kind::Clap);
        CHECK (drumkit::kindOf (chosen[3]) == drumkit::Kind::ClosedHat);
        CHECK (drumkit::kindOf (chosen[4]) == drumkit::Kind::OpenHat);
        CHECK (drumkit::kindOf (chosen[5]) == drumkit::Kind::Tom);
        CHECK (drumkit::kindOf (chosen[7]) == drumkit::Kind::Crash);
        std::set<std::string> distinct (chosen.begin(), chosen.end());
        CHECK_EQ (distinct.size(), chosen.size());
        for (auto& c : chosen) CHECK (c.find ("Loop") == std::string::npos && c.find ("Long") == std::string::npos && c.find (".mid") == std::string::npos);
        kicks.insert (chosen[0]);
    }
    CHECK (kicks.size() >= 2);   // not the same kick every time
    // An empty library: nothing for any pad.
    for (auto& c : drumkit::randomSamples (k, {}, 1)) CHECK (c.empty());
}

TEST ("drums: the pattern as a clip's notes is the drum grid's, on the pads' notes, with its swing (0105)")
{
    auto k = drumkit::standard();
    k.pattern.lanes[0].velocity[0] = 120;
    k.pattern.lanes[0].velocity[8] = 100;
    k.pattern.lanes[1].velocity[4] = 90;
    k.pattern.lanes[3].velocity[1] = 70;
    k.pattern.swing = 0.5;
    const auto m = drumkit::patternNotes (k);
    CHECK_EQ (m.notes.size(), (size_t) 4);
    int kicks = 0;
    for (auto& n : m.notes)
    {
        CHECK (n.from == NoteFrom::Drums);
        if (n.pitch == 36) ++kicks;
        if (n.pitch == 42) CHECK_EQ (n.start, PPQ / 4 + PPQ / 8);   // the second sixteenth, swung by half a step
    }
    CHECK_EQ (kicks, 2);
    const auto grid = patterns::readGrid (m, k.pattern, 0, NoteFrom::Drums);
    CHECK_EQ (grid.lanes[1].velocity[4], 90);
}

TEST ("drums: a groove's General MIDI notes go on the kit's pads: their own, else the nearest kind; the rest are left out (0105)")
{
    auto k = drumkit::standard();
    CHECK_EQ (drumkit::padFor (k, 36), 0);
    CHECK_EQ (drumkit::padFor (k, 51), 3);    // a ride, with no ride pad: on the closed hat
    CHECK_EQ (drumkit::padFor (k, 44), 3);    // the pedal hat too
    CHECK_EQ (drumkit::padFor (k, 37), 1);    // the side stick on the snare
    CHECK_EQ (drumkit::padFor (k, 48), 5);    // a tom on the first tom
    std::vector<Note> groove;
    auto hit = [&] (int pitch, Tick at, int vel) { Note n; n.pitch = pitch; n.start = at; n.length = PPQ / 8; n.velocity = vel; groove.push_back (n); };
    hit (36, 0, 120);
    hit (38, PPQ, 100);
    hit (51, PPQ / 2, 80);
    hit (36, PPQ * 8, 120);   // past the pattern's bar: left out
    drumkit::patternFrom (k, groove);
    CHECK_EQ (k.pattern.lanes[0].velocity[0], 120);
    CHECK_EQ (k.pattern.lanes[1].velocity[4], 100);
    CHECK_EQ (k.pattern.lanes[3].velocity[2], 80);
    int hits = 0;
    for (auto& l : k.pattern.lanes) for (int v : l.velocity) hits += v > 0;
    CHECK_EQ (hits, 3);
}
