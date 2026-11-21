#include "DrumKit.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <map>
#include <random>
#include <set>

namespace kal::drumkit
{
using nlohmann::json;

bool Kit::operator== (const Kit& o) const { return toJson (*this) == toJson (o); }

Kit standard()
{
    Kit k;
    const struct { const char* name; int note; int choke; } pads[] {
        { "Kick", 36, 0 }, { "Snare", 38, 0 }, { "Clap", 39, 0 }, { "Closed Hat", 42, 1 },
        { "Open Hat", 46, 1 }, { "Low Tom", 45, 0 }, { "High Tom", 50, 0 }, { "Crash", 49, 0 },
    };
    for (auto& p : pads)
    {
        Pad pad;
        pad.name = p.name;
        pad.note = p.note;
        pad.choke = p.choke;
        k.pads.push_back (pad);
    }
    k.pattern.steps = 16;
    k.pattern.stepTicks = PPQ / 4;
    matchLanes (k);
    return k;
}

void matchLanes (Kit& k)
{
    auto& g = k.pattern;
    g.steps = std::clamp (g.steps, 1, 64);
    std::vector<patterns::Lane> lanes;
    for (size_t i = 0; i < k.pads.size(); ++i)
    {
        patterns::Lane lane = i < g.lanes.size() ? g.lanes[i] : patterns::Lane {};
        lane.name = k.pads[i].name;
        lane.pitch = k.pads[i].note;
        lane.velocity.resize (static_cast<size_t> (g.steps), 0);
        lanes.push_back (std::move (lane));
    }
    g.lanes = std::move (lanes);
}

int addPad (Kit& k, const std::string& name)
{
    if ((int) k.pads.size() >= maxPads) return -1;
    std::set<int> used;
    for (auto& p : k.pads) used.insert (p.note);
    int note = 35;
    for (int n : { 36, 38, 39, 42, 46, 45, 50, 49, 37, 51, 47, 48, 41, 43, 44, 54, 56, 70, 75, 76 })
        if (! used.count (n)) { note = n; break; }
    while (used.count (note) && note < 127) ++note;
    Pad pad;
    pad.note = note;
    pad.name = name.empty() ? patterns::drumName (note) : name;
    k.pads.push_back (pad);
    matchLanes (k);
    return (int) k.pads.size() - 1;
}

void removePad (Kit& k, int index)
{
    if (index < 0 || index >= (int) k.pads.size()) return;
    k.pads.erase (k.pads.begin() + index);
    if (index < (int) k.pattern.lanes.size()) k.pattern.lanes.erase (k.pattern.lanes.begin() + index);
    matchLanes (k);
}

//==============================================================================
std::string toJson (const Kit& k)
{
    json pads = json::array();
    for (auto& p : k.pads)
        pads.push_back ({ { "name", p.name }, { "note", p.note }, { "sample", p.sample },
                          { "plugin", { { "format", p.plugin.format }, { "identifier", p.plugin.identifier }, { "name", p.plugin.name },
                                        { "manufacturer", p.plugin.manufacturer }, { "uid", p.plugin.uid },
                                        { "isInstrument", p.plugin.isInstrument }, { "description", p.plugin.description } } },
                          { "pluginState", p.pluginState }, { "pluginNote", p.pluginNote }, { "volumeDb", p.volumeDb },
                          { "pan", p.pan }, { "mute", p.mute }, { "choke", p.choke } });
    json lanes = json::array();
    for (auto& l : k.pattern.lanes) lanes.push_back (l.velocity);
    return json ({ { "kalibreDrums", 1 }, { "pads", pads }, { "steps", k.pattern.steps }, { "stepTicks", k.pattern.stepTicks },
                   { "swing", k.pattern.swing }, { "gate", k.pattern.gate }, { "lanes", lanes }, { "playsPattern", k.playsPattern } }).dump();
}

std::optional<Kit> fromJson (const std::string& text)
{
    const auto j = json::parse (text, nullptr, false);
    if (! j.is_object() || ! j.contains ("kalibreDrums")) return std::nullopt;
    try
    {
        Kit k;
        for (auto& e : j.value ("pads", json::array()))
        {
            Pad p;
            p.name = e.value ("name", std::string());
            p.note = std::clamp (e.value ("note", 36), 0, 127);
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
            p.volumeDb = e.value ("volumeDb", 0.0);
            p.pan = std::clamp (e.value ("pan", 0.0), -1.0, 1.0);
            p.mute = e.value ("mute", false);
            p.choke = std::clamp (e.value ("choke", 0), 0, 4);
            if ((int) k.pads.size() < maxPads) k.pads.push_back (p);
        }
        k.pattern.steps = std::clamp (j.value ("steps", 16), 1, 64);
        k.pattern.stepTicks = std::max<Tick> (1, j.value ("stepTicks", (Tick) (PPQ / 4)));
        k.pattern.swing = std::clamp (j.value ("swing", 0.0), 0.0, 0.75);
        k.pattern.gate = std::clamp (j.value ("gate", 0.9), 0.05, 1.0);
        for (auto& l : j.value ("lanes", json::array()))
        {
            patterns::Lane lane;
            for (auto& v : l) lane.velocity.push_back (std::clamp (v.get<int>(), 0, 127));
            k.pattern.lanes.push_back (lane);
        }
        k.playsPattern = j.value ("playsPattern", true);
        matchLanes (k);
        return k;
    }
    catch (...) { return std::nullopt; }
}

//==============================================================================
namespace
{
// What a pad would take, best first.
std::vector<Kind> wants (Kind pad)
{
    switch (pad)
    {
        case Kind::ClosedHat: return { Kind::ClosedHat, Kind::Hat };
        case Kind::OpenHat:   return { Kind::OpenHat, Kind::Hat };
        case Kind::Hat:       return { Kind::Hat, Kind::ClosedHat, Kind::OpenHat };
        case Kind::Crash:     return { Kind::Crash, Kind::Cymbal };
        case Kind::Ride:      return { Kind::Ride, Kind::Cymbal };
        case Kind::Cymbal:    return { Kind::Cymbal, Kind::Crash, Kind::Ride };
        case Kind::Rim:       return { Kind::Rim, Kind::Percussion };
        case Kind::Shaker:    return { Kind::Shaker, Kind::Percussion };
        case Kind::Other:     return {};
        default:              return { pad };
    }
}
} // namespace

// Only drums (decision 0106: name, length and shape), each pad a sound of its kind;
// a pad whose kind none has takes a drum of no kind its name says.
std::vector<std::string> randomSamples (const Kit& k, const library::Index& index, unsigned seed)
{
    std::map<Kind, std::vector<std::string>> byKind;
    for (auto& [path, it] : index.items)
    {
        const auto v = drumsound::of (it);
        if (v.drum) byKind[v.kind].push_back (path);
    }
    std::mt19937 rng (seed);
    std::set<std::string> taken;
    auto pick = [&] (const std::vector<std::string>& from) -> std::string
    {
        std::vector<std::string> free;
        for (auto& p : from) if (! taken.count (p)) free.push_back (p);
        if (free.empty()) return {};
        std::uniform_int_distribution<size_t> d (0, free.size() - 1);
        return free[d (rng)];
    };
    std::vector<std::string> out;
    for (auto& pad : k.pads)
    {
        std::string chosen;
        for (auto kind : wants (kindOf (pad.name)))
            if (auto it = byKind.find (kind); it != byKind.end())
                if (chosen = pick (it->second); ! chosen.empty()) break;
        if (chosen.empty())
        {
            std::vector<std::string> spare;
            for (auto kind : { Kind::Other, Kind::Percussion })
                if (auto it = byKind.find (kind); it != byKind.end()) spare.insert (spare.end(), it->second.begin(), it->second.end());
            chosen = pick (spare);
        }
        if (! chosen.empty()) taken.insert (chosen);
        out.push_back (chosen);
    }
    return out;
}

//==============================================================================
int padFor (const Kit& k, int note)
{
    for (size_t i = 0; i < k.pads.size(); ++i) if (k.pads[i].note == note) return (int) i;
    // General MIDI's name for the note says what it is.
    const auto kind = kindOf (patterns::drumName (note));
    std::vector<Kind> nearest;
    switch (kind)
    {
        case Kind::Ride: case Kind::Cymbal: nearest = { Kind::Ride, Kind::ClosedHat, Kind::Hat, Kind::Crash }; break;
        case Kind::Hat: nearest = { Kind::ClosedHat, Kind::Hat, Kind::OpenHat }; break;   // the pedal hat
        case Kind::Rim: nearest = { Kind::Rim, Kind::Snare, Kind::Clap }; break;
        case Kind::Clap: nearest = { Kind::Clap, Kind::Snare }; break;
        case Kind::Crash: nearest = { Kind::Crash, Kind::Cymbal, Kind::Ride }; break;
        case Kind::Other: case Kind::Percussion: case Kind::Shaker: nearest = { kind, Kind::Percussion, Kind::Shaker }; break;
        default: nearest = { kind }; break;
    }
    for (auto want : nearest)
        for (size_t i = 0; i < k.pads.size(); ++i)
            if (kindOf (k.pads[i].name) == want) return (int) i;
    return -1;
}

void patternFrom (Kit& k, const std::vector<Note>& notes)
{
    matchLanes (k);
    for (auto& lane : k.pattern.lanes) std::fill (lane.velocity.begin(), lane.velocity.end(), 0);
    const Tick step = std::max<Tick> (1, k.pattern.stepTicks);
    for (auto& n : notes)
    {
        const int pad = padFor (k, n.pitch);
        if (pad < 0) continue;
        const auto s = (Tick) std::llround ((double) n.start / (double) step);
        if (s < 0 || s >= k.pattern.steps) continue;
        auto& v = k.pattern.lanes[(size_t) pad].velocity[(size_t) s];
        v = std::max (v, std::clamp (n.velocity, 1, 127));
    }
}

//==============================================================================
MidiData patternNotes (const Kit& k)
{
    MidiData m;
    unsigned id = 1;
    patterns::writeGrid (m, k.pattern, 0, patternLength (k), id, NoteFrom::Drums);
    return m;
}

Tick patternLength (const Kit& k) { return k.pattern.stepTicks * std::max (1, k.pattern.steps); }

} // namespace kal::drumkit
