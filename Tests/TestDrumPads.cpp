#include "Check.h"
#include "DrumPads.h"

#include <nlohmann/json.hpp>

using namespace kal;

TEST ("drum pads: 128 pads, one a note, named by General MIDI or their note; sixteen shown from the kick; the hats one choke group (0113)")
{
    const auto k = drumpads::standard();
    CHECK_EQ (k.pads.size(), (size_t) 128);
    CHECK_EQ (k.view, 36);
    CHECK_EQ (drumpads::nameOf (k, 36), std::string ("Kick"));
    CHECK_EQ (drumpads::nameOf (k, 46), std::string ("Open Hat"));
    CHECK_EQ (drumpads::nameOf (k, 0), std::string ("C-1"));
    CHECK_EQ (drumpads::nameOf (k, 96), std::string ("C7"));
    CHECK_EQ (drumpads::noteName (60), std::string ("C4"));
    CHECK (k.pads[42].choke > 0);
    CHECK_EQ (k.pads[42].choke, k.pads[46].choke);
    CHECK_EQ (k.pads[44].choke, k.pads[46].choke);
    CHECK_EQ (k.pads[36].choke, 0);
    CHECK (drumpads::sounding (k).empty());
    auto named = k;
    named.pads[36].name = "Big Kick";
    CHECK_EQ (drumpads::nameOf (named, 36), std::string ("Big Kick"));
}

TEST ("drum pads: the view is a row of four at a time, 0 to 112; a pad is shown with the row below it (0113)")
{
    CHECK_EQ (drumpads::clampView (36), 36);
    CHECK_EQ (drumpads::clampView (38), 36);
    CHECK_EQ (drumpads::clampView (-8), 0);
    CHECK_EQ (drumpads::clampView (127), 112);
    CHECK_EQ (drumpads::viewFor (60), 56);
    CHECK_EQ (drumpads::viewFor (1), 0);
    CHECK_EQ (drumpads::viewFor (127), 112);
    for (int p = 0; p < 128; ++p)
    {
        const int v = drumpads::viewFor (p);
        CHECK (p >= v && p < v + drumpads::shown);
    }
}

TEST ("drum pads: mute silences a pad; solo silences every pad not soloed; a muted soloed pad is silent (0113)")
{
    auto k = drumpads::standard();
    CHECK (drumpads::heard (k, 36));
    k.pads[38].mute = true;
    CHECK (! drumpads::heard (k, 38));
    CHECK (drumpads::heard (k, 36));
    k.pads[42].solo = true;
    CHECK (drumpads::anySolo (k));
    CHECK (drumpads::heard (k, 42));
    CHECK (! drumpads::heard (k, 36));
    k.pads[36].solo = true;
    CHECK (drumpads::heard (k, 36));
    k.pads[36].mute = true;
    CHECK (! drumpads::heard (k, 36));
    CHECK (! drumpads::heard (k, 200));
}

TEST ("drum pads: a kit comes back from its state, plug-ins and all; only pads with something on them are written (0113)")
{
    auto k = drumpads::standard();
    k.pads[36].sample = "/Sounds/Kicks/Deep.wav";
    k.pads[36].volumeDb = -4.5;
    k.pads[36].pan = -0.25;
    k.pads[38].plugin.format = "VST3";
    k.pads[38].plugin.name = "Drum Synth";
    k.pads[38].pluginState = "QUJD";
    k.pads[38].pluginNote = 48;
    k.pads[38].solo = true;
    k.pads[127].name = "Last";
    k.pads[127].mute = true;
    k.pads[127].choke = 8;
    k.pads[0].sample = "/Sounds/zero.wav";
    k.view = 112;
    const auto text = drumpads::toJson (k);
    const auto back = drumpads::fromJson (text);
    CHECK (back.has_value());
    if (! back) return;
    CHECK (*back == k);
    CHECK_EQ (drumpads::sounding (*back), (std::vector<int> { 0, 36, 38 }));
    // Seven pads written (three hats, four edited), not 128.
    const auto j = nlohmann::json::parse (text);
    CHECK_EQ (j["pads"].size(), (size_t) 7);
    // Out of range: kept in range.
    const auto odd = drumpads::fromJson (R"({"kalibreDrumPads":1,"view":53,"pads":[{"note":200,"sample":"x"},{"note":5,"volumeDb":40,"choke":99}]})");
    CHECK (odd.has_value());
    if (odd)
    {
        CHECK_EQ (odd->view, 52);
        CHECK_EQ (odd->pads[5].volumeDb, 12.0);
        CHECK_EQ (odd->pads[5].choke, drumpads::chokeGroups);
    }
    CHECK (! drumpads::fromJson ("{\"kalibreDrums\":1}").has_value());
    CHECK (! drumpads::fromJson ("not json").has_value());
}

TEST ("drum pads: Random Samples gives each pad shown a drum of its kind, never one twice (0113)")
{
    library::Index ix;
    for (auto* name : { "Kick 1", "Snare 1", "HH Closed", "HH Open", "Pad Long" })
    {
        library::Item it;
        it.path = std::string ("/S/") + name + ".wav";
        it.readable = true;
        it.seconds = std::string (name) == "Pad Long" ? 8.0 : 0.2;
        ix.items[it.path] = it;
    }
    const auto k = drumpads::standard();
    const std::vector<int> pads { 36, 38, 42, 46 };
    const auto chosen = drumpads::randomSamples (k, pads, ix, 7);
    CHECK_EQ (chosen.size(), (size_t) 4);
    if (chosen.size() != 4) return;
    CHECK_EQ (chosen[0], std::string ("/S/Kick 1.wav"));
    CHECK_EQ (chosen[1], std::string ("/S/Snare 1.wav"));
    CHECK_EQ (chosen[2], std::string ("/S/HH Closed.wav"));
    CHECK_EQ (chosen[3], std::string ("/S/HH Open.wav"));
    const auto dk = drumpads::asDrumKit (k, pads);
    CHECK_EQ (dk.pads.size(), (size_t) 4);
    CHECK_EQ (dk.pads[2].note, 42);
    CHECK_EQ (dk.pads[2].name, std::string ("Closed Hat"));
}
