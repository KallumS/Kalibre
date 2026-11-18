// Importing articulation maps (decision 0072): Cubase expression maps, Logic
// articulation sets and Reaticulate banks, read as Note Position Fixer reads
// them (its test files, unchanged).
#include "Check.h"
#include "Articulations.h"
#include "Playback.h"
#include "Edit.h"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace kal;

static std::string fixture (const std::string& name)
{
    std::ifstream in (std::filesystem::path (KALIBRE_SOURCE_DIR) / "Tests" / "fixtures" / "articulations" / name, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

static const Articulation* named (const ArticulationMap& m, const std::string& n)
{
    for (auto& a : m.articulations) if (a.name == n) return &a;
    return nullptr;
}

TEST ("articulations: a Cubase expression map, its keyswitches, controllers, programs and channels")
{
    const auto r = articulations::import (fixture ("Test Strings.expressionmap"), "Test Strings.expressionmap");
    CHECK (r.error.empty());
    CHECK_EQ (r.format, std::string ("Cubase expression map"));
    CHECK_EQ (r.maps.size(), size_t (1));
    if (r.maps.empty()) return;
    const auto& m = r.maps.front();
    CHECK_EQ (m.name, std::string ("Test Strings & Co"));
    const auto* stacc = named (m, "Staccato");
    CHECK (stacc != nullptr);
    if (stacc) { CHECK_EQ (stacc->keyswitch, 12); CHECK_EQ (stacc->keyswitchVelocity, 127); }
    // Two keyswitches and a controller: the first of each kind in its place, the rest sent too.
    const auto* longMute = named (m, "Long + Mute");
    CHECK (longMute != nullptr);
    if (longMute)
    {
        CHECK_EQ (longMute->keyswitch, 13);
        CHECK_EQ (longMute->cc, 20);
        CHECK_EQ (longMute->ccValue, 64);
        CHECK_EQ (longMute->more.size(), size_t (1));
        if (! longMute->more.empty()) { CHECK_EQ (longMute->more[0].number, 50); CHECK_EQ (longMute->more[0].value, 100); }
        CHECK_EQ (articulations::describe (*longMute), std::string ("C#0 + D3 vel 100 + CC20 = 64"));
    }
    const auto* pizz = named (m, "Pizzicato");
    CHECK (pizz != nullptr);
    if (pizz) { CHECK_EQ (pizz->program, 7); CHECK_EQ (pizz->channel, 2); }
}

TEST ("articulations: a Logic articulation set")
{
    const auto r = articulations::import (fixture ("Test Strings.plist"), "Test Strings.plist");
    CHECK (r.error.empty());
    CHECK_EQ (r.maps.size(), size_t (1));
    if (r.maps.empty()) return;
    const auto& m = r.maps.front();
    CHECK_EQ (m.name, std::string ("Test Strings"));
    const auto* lg = named (m, "Long Generic");
    CHECK (lg != nullptr);
    if (lg) CHECK_EQ (lg->keyswitch, 0);
    const auto* sp = named (m, "Spiccato & Short");
    CHECK (sp != nullptr);
    if (sp) { CHECK_EQ (sp->cc, 32); CHECK_EQ (sp->ccValue, 42); CHECK_EQ (sp->keyswitch, 24); CHECK_EQ (sp->keyswitchVelocity, 100); }
}

TEST ("articulations: Reaticulate banks, each a map; hidden ones left out")
{
    const auto r = articulations::import (fixture ("Test Strings.reabank"), "Test Strings.reabank");
    CHECK (r.error.empty());
    CHECK_EQ (r.maps.size(), size_t (2));
    if (r.maps.size() != 2) return;
    CHECK_EQ (r.maps[0].name, std::string ("Violins"));
    CHECK (named (r.maps[0], "Hidden One") == nullptr);
    const auto* leg = named (r.maps[0], "Legato");
    CHECK (leg != nullptr);
    if (leg) { CHECK_EQ (leg->keyswitch, 12); CHECK_EQ (leg->more.size(), size_t (1)); }
    const auto* pizz = named (r.maps[0], "Pizzicato");
    CHECK (pizz != nullptr);
    if (pizz) { CHECK_EQ (pizz->cc, 32); CHECK_EQ (pizz->ccValue, 56); CHECK_EQ (pizz->channel, 2); }
    CHECK_EQ (r.maps[1].name, std::string ("Cellos"));
    const auto* sus = named (r.maps[1], "Sustain");
    CHECK (sus != nullptr);
    if (sus) CHECK_EQ (sus->program, 5);

    // Not a map at all.
    CHECK (! articulations::import ("hello", "notes.txt").error.empty());
}

TEST ("articulations: every output of an imported articulation is sent before its note; the library round-trips")
{
    const auto r = articulations::import (fixture ("Test Strings.expressionmap"), "x.expressionmap");
    CHECK (! r.maps.empty());
    if (r.maps.empty()) return;
    Project p;
    auto map = r.maps.front();
    map.id = p.newId();
    p.articulationMaps.push_back (map);
    const Id t = edit::addTrack (p, TrackKind::Track, "Strings");
    p.track (t)->articulationMap = map.id;
    const Id clip = edit::addMidiClip (p, t, PPQ * 4, PPQ * 4);
    Note n; n.start = 0; n.length = PPQ; n.pitch = 60;
    for (size_t i = 0; i < map.articulations.size(); ++i) if (map.articulations[i].name == "Long + Mute") n.articulation = (int) i;
    edit::addNote (p, clip, n);
    const auto compiled = playback::compile (p, *p.track (t));
    bool k13 = false, k50 = false, cc20 = false;
    for (auto& m : compiled.midi)
    {
        if ((m.bytes[0] & 0xF0) == 0x90 && m.bytes[1] == 13) k13 = true;
        if ((m.bytes[0] & 0xF0) == 0x90 && m.bytes[1] == 50 && m.bytes[2] == 100) k50 = true;
        if ((m.bytes[0] & 0xF0) == 0xB0 && m.bytes[1] == 20 && m.bytes[2] == 64) cc20 = true;
    }
    CHECK (k13 && k50 && cc20);

    const auto saved = articulations::saveLibrary (r.maps);
    const auto loaded = articulations::loadLibrary (saved);
    CHECK (loaded == r.maps);
}
