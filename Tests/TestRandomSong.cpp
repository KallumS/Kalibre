// The Random button's song (decision 0101): small enough to play, made of the
// engines' music, the same song from the same seed.
#include "Check.h"
#include "RandomSong.h"

#include <set>

using namespace kal;
using namespace kal::randomsong;

namespace
{
Choice choice (const std::string& name, DeviceRole role, bool drums = false, bool wantsSample = false)
{
    Choice c;
    c.name = name;
    c.ref.format = "Kalibre";
    c.ref.identifier = name;
    c.ref.name = name;
    c.role = role;
    c.drums = drums;
    c.wantsSample = wantsSample;
    return c;
}

Catalogue catalogue (bool withSounds)
{
    Catalogue c;
    c.instruments = { choice ("Kalibre Synth", DeviceRole::Instrument), choice ("Trombolese", DeviceRole::Instrument),
                      choice ("Kalibre Sampler", DeviceRole::Instrument, false, true), choice ("Drum Kit", DeviceRole::Instrument, true) };
    for (int i = 0; i < 6; ++i) c.effects.push_back (choice ("Effect " + std::to_string (i), DeviceRole::AudioFx));
    c.midiEffects = { choice ("Arpeggiator", DeviceRole::MidiFx), choice ("Chord Maker", DeviceRole::MidiFx) };
    if (withSounds)
    {
        Sample loop;
        loop.path = "/s/Break 96bpm.wav"; loop.name = "Break 96bpm"; loop.seconds = 5.0; loop.bpm = 96; loop.drums = true;
        Sample loop2 = loop;
        loop2.path = "/s/Keys 120bpm.wav"; loop2.name = "Keys 120bpm"; loop2.seconds = 4.0; loop2.bpm = 120; loop2.drums = false;
        Sample hit;
        hit.path = "/s/Pluck.wav"; hit.name = "Pluck"; hit.seconds = 0.8; hit.note = 57;
        c.samples = { loop, loop2, hit };
    }
    return c;
}

int countRole (const Project& p, DeviceRole role)
{
    int n = 0;
    for (auto& t : p.tracks) for (auto& d : t.devices) n += d.role == role;
    return n;
}
} // namespace

TEST ("random song: 4 to 8 tracks, effects kept few, drums on a kit, every part with music; the same seed, the same song (0101)")
{
    const auto cat = catalogue (false);
    std::set<size_t> counts;
    for (unsigned seed = 1; seed <= 40; ++seed)
    {
        Options o;
        o.seed = seed;
        const auto r = make (cat, o);
        const auto& p = r.project;
        counts.insert (p.tracks.size());
        CHECK (p.tracks.size() >= 4 && p.tracks.size() <= 8);
        CHECK (countRole (p, DeviceRole::AudioFx) <= 8);
        CHECK (countRole (p, DeviceRole::MidiFx) <= 2);
        CHECK (p.loop.on && p.loop.end == PPQ * 32);
        const double bpm = p.tempo.bpmAt (0);
        CHECK (bpm >= 82 && bpm <= 148);
        for (auto& t : p.tracks)
        {
            int fx = 0;
            for (auto& d : t.devices) fx += d.role == DeviceRole::AudioFx;
            CHECK (fx <= 2);
            CHECK_EQ (t.clips().size(), (size_t) 1);
            if (t.clips().empty()) continue;
            const auto& c = t.clips().front();
            CHECK (c.isMidi());
            CHECK (! c.midi.notes.empty());
            int instruments = 0;
            for (auto& d : t.devices) instruments += d.role == DeviceRole::Instrument;
            CHECK_EQ (instruments, 1);
            // The Sampler only where there's a sound to give it: none here.
            for (auto& d : t.devices) CHECK (d.plugin.name != "Kalibre Sampler");
            if (t.name == "Drums") CHECK (t.devices.back().plugin.name == "Drum Kit" || t.devices[0].plugin.name == "Drum Kit");
        }
        CHECK (! r.summary.empty());
        CHECK_EQ (r.parts.size(), p.tracks.size());
    }
    CHECK (counts.size() >= 3);   // not always the same size
    // Drums, bass, chords and a lead are always there (the engines worked).
    Options o;
    o.seed = 7;
    const auto a = make (cat, o);
    std::set<std::string> names;
    for (auto& t : a.project.tracks) names.insert (t.name);
    for (auto* n : { "Drums", "Bass", "Chords", "Lead" }) CHECK (names.count (n) == 1);
    // The same seed: the same song.
    const auto b = make (cat, o);
    CHECK (a.project.tracks.size() == b.project.tracks.size());
    for (size_t i = 0; i < a.project.tracks.size() && i < b.project.tracks.size(); ++i)
    {
        CHECK (a.project.tracks[i].name == b.project.tracks[i].name);
        CHECK (a.project.tracks[i].clips().front().midi.notes == b.project.tracks[i].clips().front().midi.notes);
    }
}

TEST ("random song: loops from the musician's sounds, stretched to the tempo; the Sampler given a sound (0101)")
{
    const auto cat = catalogue (true);
    bool sawLoop = false, sawSampler = false;
    for (unsigned seed = 1; seed <= 60; ++seed)
    {
        Options o;
        o.seed = seed;
        const auto r = make (cat, o);
        for (auto& t : r.project.tracks)
            for (auto& c : t.clips())
                if (! c.isMidi())
                {
                    sawLoop = true;
                    CHECK (c.audio.warp == WarpMode::Spectral);
                    CHECK (c.audio.sourceBpm == 96 || c.audio.sourceBpm == 120);
                    CHECK (c.loopLength > 0 && c.length == PPQ * 32);
                    CHECK (t.devices.size() <= 2);   // a loop has no instrument
                }
        for (auto& [device, path] : r.samplerSamples)
        {
            sawSampler = true;
            CHECK (const_cast<Project&> (r.project).device (device) != nullptr);
            CHECK (path == "/s/Pluck.wav" || path == "/s/Break 96bpm.wav");
        }
    }
    CHECK (sawLoop);
    CHECK (sawSampler);
}

