#include "DrumPads.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace kal::drumpads
{
using nlohmann::json;

Kit standard()
{
    Kit k;
    for (int hat : { 42, 44, 46 }) k.pads[(size_t) hat].choke = 1;   // closed, pedal and open hat: each stops the others
    k.view = 36;
    return k;
}

std::string noteName (int note) { return library::noteName (note, 0); }

std::string nameOf (const Kit& k, int pad)
{
    if (pad < 0 || pad >= padCount) return {};
    const auto& p = k.pads[(size_t) pad];
    if (! p.name.empty()) return p.name;
    if (pad >= 35 && pad <= 81) return patterns::drumName (pad);
    return noteName (pad);
}

bool anySolo (const Kit& k)
{
    return std::any_of (k.pads.begin(), k.pads.end(), [] (const Pad& p) { return p.solo; });
}

bool heard (const Kit& k, int pad)
{
    if (pad < 0 || pad >= padCount) return false;
    const auto& p = k.pads[(size_t) pad];
    return ! p.mute && (p.solo || ! anySolo (k));
}

int clampView (int view) { return std::clamp (view / perRow * perRow, 0, padCount - shown); }
int viewFor (int pad) { return clampView (pad / perRow * perRow - perRow); }   // a row below it shown too, where there is one

std::vector<int> sounding (const Kit& k)
{
    std::vector<int> out;
    for (int i = 0; i < padCount; ++i) if (k.pads[(size_t) i].hasSound()) out.push_back (i);
    return out;
}

//==============================================================================
std::string toJson (const Kit& k)
{
    const Pad empty;
    json pads = json::array();
    for (int i = 0; i < padCount; ++i)
    {
        const auto& p = k.pads[(size_t) i];
        if (p == empty) continue;
        pads.push_back ({ { "note", i }, { "name", p.name }, { "sample", p.sample },
                          { "plugin", { { "format", p.plugin.format }, { "identifier", p.plugin.identifier }, { "name", p.plugin.name },
                                        { "manufacturer", p.plugin.manufacturer }, { "uid", p.plugin.uid },
                                        { "isInstrument", p.plugin.isInstrument }, { "description", p.plugin.description } } },
                          { "pluginState", p.pluginState }, { "pluginNote", p.pluginNote }, { "volumeDb", p.volumeDb },
                          { "pan", p.pan }, { "mute", p.mute }, { "solo", p.solo }, { "choke", p.choke } });
    }
    return json ({ { "kalibreDrumPads", 1 }, { "pads", pads }, { "view", k.view } }).dump();
}

std::optional<Kit> fromJson (const std::string& text)
{
    const auto j = json::parse (text, nullptr, false);
    if (! j.is_object() || ! j.contains ("kalibreDrumPads")) return std::nullopt;
    try
    {
        Kit k;   // pads not written are empty (the hats' choke group is written with them)
        for (auto& e : j.value ("pads", json::array()))
        {
            const int note = e.value ("note", -1);
            if (note < 0 || note >= padCount) continue;
            auto& p = k.pads[(size_t) note];
            p.name = e.value ("name", std::string());
            p.sample = e.value ("sample", std::string());
            if (e.contains ("plugin") && e["plugin"].is_object())
            {
                const auto& r = e["plugin"];
                p.plugin.format = r.value ("format", std::string());
                p.plugin.identifier = r.value ("identifier", std::string());
                p.plugin.name = r.value ("name", std::string());
                p.plugin.manufacturer = r.value ("manufacturer", std::string());
                p.plugin.uid = r.value ("uid", 0);
                p.plugin.isInstrument = r.value ("isInstrument", false);
                p.plugin.description = r.value ("description", std::string());
            }
            p.pluginState = e.value ("pluginState", std::string());
            p.pluginNote = std::clamp (e.value ("pluginNote", 60), 0, 127);
            p.volumeDb = std::clamp (e.value ("volumeDb", 0.0), -36.0, 12.0);
            p.pan = std::clamp (e.value ("pan", 0.0), -1.0, 1.0);
            p.mute = e.value ("mute", false);
            p.solo = e.value ("solo", false);
            p.choke = std::clamp (e.value ("choke", 0), 0, chokeGroups);
        }
        k.view = clampView (j.value ("view", 36));
        return k;
    }
    catch (...) { return std::nullopt; }
}

//==============================================================================
drumkit::Kit asDrumKit (const Kit& k, const std::vector<int>& pads)
{
    drumkit::Kit out;
    for (int i : pads)
    {
        if (i < 0 || i >= padCount) continue;
        drumkit::Pad p;
        p.name = nameOf (k, i);
        p.note = i;
        out.pads.push_back (p);
    }
    return out;
}

std::vector<std::string> randomSamples (const Kit& k, const std::vector<int>& pads, const library::Index& index, unsigned seed)
{
    return drumkit::randomSamples (asDrumKit (k, pads), index, seed);
}

} // namespace kal::drumpads
