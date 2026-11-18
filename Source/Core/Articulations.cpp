#include "Articulations.h"

#include "ProjectFile.h"
#include "Xml.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace kal::articulations
{
namespace
{
struct Output { int kind = ArtOutput::Note; int number = 0; int value = 127; };

// Colours for the articulation lane, in turn.
const std::uint32_t palette[] { 0xff6FA8DC, 0xff7FC8B6, 0xffE3C26F, 0xffE89A6B, 0xffB892D9, 0xffE07A86, 0xff9CC97F, 0xff8FB3E8 };

Articulation make (const std::string& name, const std::vector<Output>& outs, int channel, size_t index)
{
    Articulation a;
    a.name = name;
    a.channel = channel;
    a.colour = palette[index % std::size (palette)];
    for (auto& o : outs)
    {
        if (o.kind == ArtOutput::Note && a.keyswitch < 0) { a.keyswitch = o.number; a.keyswitchVelocity = std::clamp (o.value, 1, 127); }
        else if (o.kind == ArtOutput::Controller && a.cc < 0) { a.cc = o.number; a.ccValue = o.value; }
        else if (o.kind == ArtOutput::Program && a.program < 0) a.program = o.number;
        else a.more.push_back ({ o.kind, o.number, o.value });
    }
    return a;
}

int toInt (const std::string& s, int fallback)
{
    if (s.empty()) return fallback;
    char* end = nullptr;
    const long v = std::strtol (s.c_str(), &end, 10);
    return end == s.c_str() ? fallback : (int) v;
}

std::string lower (std::string s)
{
    for (auto& ch : s) ch = (char) std::tolower ((unsigned char) ch);
    return s;
}

//==============================================================================
// Cubase expression maps: <obj class="PSoundSlot"> each, with its name, and
// in its action the output events and the action's own channel.
const xml::Node* childNamed (const xml::Node& n, const std::string& element, const std::string& nameAttr)
{
    for (auto& c : n.children)
        if (c.name == element && c.attr ("name") == nameAttr) return &c;
    return nullptr;
}

void findAll (const xml::Node& n, const std::string& cls, std::vector<const xml::Node*>& out)
{
    for (auto& c : n.children)
    {
        if (c.name == "obj" && c.attr ("class") == cls) out.push_back (&c);
        findAll (c, cls, out);
    }
}

int intValue (const xml::Node& n, const std::string& name, int fallback)
{
    if (auto* v = childNamed (n, "int", name)) return toInt (v->attr ("value"), fallback);
    return fallback;
}

Imported cubase (const xml::Node& root)
{
    Imported r;
    r.format = "Cubase expression map";
    ArticulationMap map;
    if (auto* nm = childNamed (root, "string", "name")) map.name = nm->attr ("value");
    const auto* slots = childNamed (root, "member", "slots");
    if (! slots) { r.error = "The expression map has no articulations."; return r; }
    std::vector<const xml::Node*> found;
    findAll (*slots, "PSoundSlot", found);
    for (auto* slot : found)
    {
        std::string name;
        if (auto* m = childNamed (*slot, "member", "name"))
            if (auto* s = childNamed (*m, "string", "s")) name = s->attr ("value");
        std::vector<Output> outs;
        int channel = -1;
        const xml::Node* action = nullptr;
        for (auto& c : slot->children) if (c.name == "obj" && c.attr ("class") == "PSlotMidiAction") action = &c;
        if (action)
        {
            if (auto* msgs = childNamed (*action, "member", "midiMessages"))
            {
                std::vector<const xml::Node*> events;
                findAll (*msgs, "POutputEvent", events);
                for (auto* e : events)
                {
                    const int status = intValue (*e, "status", -1), d1 = intValue (*e, "data1", -1), d2 = intValue (*e, "data2", 0);
                    if (status < 0 || d1 < 0) continue;
                    const int kind = status & 0xF0;
                    if (kind == 0x90) outs.push_back ({ ArtOutput::Note, d1, d2 > 0 ? d2 : 127 });
                    else if (kind == 0xB0) outs.push_back ({ ArtOutput::Controller, d1, d2 });
                    else if (kind == 0xC0) outs.push_back ({ ArtOutput::Program, d1, 0 });
                }
            }
            // The action's own channel, not its note changer's.
            channel = intValue (*action, "channel", -1);
        }
        const bool empty = outs.empty() && channel < 0;
        if (name.empty() || (empty && lower (name) == "empty")) continue;
        map.articulations.push_back (make (name, outs, channel >= 0 ? (channel & 15) : -1, map.articulations.size()));
    }
    r.maps.push_back (map);
    return r;
}

//==============================================================================
// Logic articulation sets: a property list, its Articulations an array of dicts.
struct PValue
{
    enum Type { None, Dict, Array, Text, Number } type = None;
    std::vector<std::pair<std::string, PValue>> dict;
    std::vector<PValue> array;
    std::string text;
    double number = 0;

    const PValue* get (const std::string& k) const
    {
        for (auto& [key, v] : dict) if (key == k) return &v;
        return nullptr;
    }
    int asInt (int fallback) const { return type == Number ? (int) number : type == Text ? toInt (text, fallback) : fallback; }
};

PValue plistValue (const xml::Node& n)
{
    PValue v;
    if (n.name == "dict")
    {
        v.type = PValue::Dict;
        for (size_t i = 0; i + 1 < n.children.size(); ++i)
            if (n.children[i].name == "key") { v.dict.push_back ({ n.children[i].text, plistValue (n.children[i + 1]) }); ++i; }
    }
    else if (n.name == "array")
    {
        v.type = PValue::Array;
        for (auto& c : n.children) v.array.push_back (plistValue (c));
    }
    else if (n.name == "integer" || n.name == "real") { v.type = PValue::Number; v.number = std::atof (n.text.c_str()); }
    else if (n.name == "string") { v.type = PValue::Text; v.text = n.text; }
    return v;
}

Imported logic (const xml::Node& root)
{
    Imported r;
    r.format = "Logic articulation set";
    const xml::Node* top = &root;
    if (root.name == "plist" && ! root.children.empty()) top = &root.children.front();
    const auto doc = plistValue (*top);
    const auto* arts = doc.get ("Articulations");
    if (doc.type != PValue::Dict || ! arts || arts->type != PValue::Array) { r.error = "This isn't a Logic articulation set."; return r; }
    ArticulationMap map;
    if (auto* n = doc.get ("Name")) map.name = n->text;
    for (auto& a : arts->array)
    {
        const auto* name = a.get ("Name");
        if (! name || name->text.empty()) continue;
        std::vector<const PValue*> outs;
        if (auto* o = a.get ("Output"))
        {
            if (o->type == PValue::Dict) outs.push_back (o);
            else for (auto& x : o->array) outs.push_back (&x);
        }
        std::vector<Output> sends;
        int channel = -1;
        for (auto* o : outs)
        {
            const auto* st = o->get ("Status");
            const std::string status = st ? lower (st->text) : std::string();
            if (auto* ch = o->get ("Channel"); ch && ch->asInt (0) >= 1) channel = (ch->asInt (1) - 1) & 15;
            const auto* mb1 = o->get ("MB1");
            const int low = o->get ("ValueLow") ? o->get ("ValueLow")->asInt (0) : 0;
            if (status == "note on" && mb1) sends.push_back ({ ArtOutput::Note, mb1->asInt (0), low > 0 ? low : 127 });
            else if (status == "controller" && mb1) sends.push_back ({ ArtOutput::Controller, mb1->asInt (0), low });
            else if (status.find ("program") != std::string::npos) sends.push_back ({ ArtOutput::Program, low > 0 ? low : (mb1 ? mb1->asInt (0) : 0), 0 });
        }
        map.articulations.push_back (make (name->text, sends, channel, map.articulations.size()));
    }
    r.maps.push_back (map);
    return r;
}

//==============================================================================
// Reaticulate: "//!" lines hold an articulation's properties (o= its outputs,
// like note:24/note:36,100/cc:32,42/program:5/note@3:24), then "N Name".
std::vector<std::pair<std::string, std::string>> props (const std::string& line)
{
    std::vector<std::pair<std::string, std::string>> out;
    size_t i = 0;
    while (i < line.size())
    {
        while (i < line.size() && ! std::isalnum ((unsigned char) line[i])) ++i;
        size_t k = i;
        while (k < line.size() && std::isalnum ((unsigned char) line[k])) ++k;
        if (k >= line.size() || line[k] != '=') { i = k + 1; continue; }
        const std::string key = line.substr (i, k - i);
        size_t v = k + 1;
        std::string value;
        if (v < line.size() && line[v] == '"')
        {
            const auto e = line.find ('"', v + 1);
            value = line.substr (v + 1, (e == std::string::npos ? line.size() : e) - v - 1);
            i = e == std::string::npos ? line.size() : e + 1;
        }
        else
        {
            size_t e = v;
            while (e < line.size() && ! std::isspace ((unsigned char) line[e])) ++e;
            value = line.substr (v, e - v);
            i = e;
        }
        out.push_back ({ key, value });
    }
    return out;
}

std::string prop (const std::vector<std::pair<std::string, std::string>>& p, const std::string& k)
{
    for (auto& [key, v] : p) if (key == k) return v;
    return {};
}

std::vector<Output> reaticulateOutputs (const std::string& spec, int& channel)
{
    std::vector<Output> sends;
    std::stringstream ss (spec);
    std::string part;
    while (std::getline (ss, part, '/'))
    {
        if (part.empty()) continue;
        size_t i = part[0] == '-' ? 1 : 0;
        size_t k = i;
        while (k < part.size() && (std::isalpha ((unsigned char) part[k]) || part[k] == '-')) ++k;
        const std::string kind = part.substr (i, k - i);
        if (auto at = part.find ('@'); at != std::string::npos) channel = (toInt (part.substr (at + 1), 1) - 1) & 15;
        std::vector<int> args;
        if (auto colon = part.find (':'); colon != std::string::npos)
        {
            std::string a = part.substr (colon + 1);
            if (auto cut = a.find_first_of ("@%"); cut != std::string::npos) a = a.substr (0, cut);
            std::stringstream as (a);
            std::string x;
            while (std::getline (as, x, ',')) args.push_back (toInt (x, 0));
        }
        if ((kind == "note" || kind == "note-hold" || kind == "note-on") && ! args.empty())
            sends.push_back ({ ArtOutput::Note, args[0], args.size() > 1 ? args[1] : 127 });
        else if (kind == "cc" && ! args.empty()) sends.push_back ({ ArtOutput::Controller, args[0], args.size() > 1 ? args[1] : 0 });
        else if (kind == "program" && ! args.empty()) sends.push_back ({ ArtOutput::Program, args[0], 0 });
    }
    return sends;
}

Imported reaticulate (const std::string& text)
{
    Imported r;
    r.format = "Reaticulate bank";
    std::vector<std::pair<std::string, std::string>> meta;
    ArticulationMap* bank = nullptr;
    std::stringstream ss (text);
    std::string line;
    while (std::getline (ss, line))
    {
        while (! line.empty() && std::isspace ((unsigned char) line.back())) line.pop_back();
        size_t s = 0;
        while (s < line.size() && std::isspace ((unsigned char) line[s])) ++s;
        line = line.substr (s);
        if (line.size() > 5 && lower (line.substr (0, 5)) == "bank ")
        {
            // Bank <msb> <lsb> <name>
            std::stringstream ls (line.substr (5));
            std::string msb, lsb, name;
            ls >> msb >> lsb;
            std::getline (ls, name);
            name.erase (0, name.find_first_not_of (' '));
            ArticulationMap m;
            m.name = ! prop (meta, "n").empty() ? prop (meta, "n") : name;
            r.maps.push_back (m);
            bank = &r.maps.back();
            meta.clear();
        }
        else if (line.rfind ("//!", 0) == 0)
        {
            for (auto& p : props (line.substr (3))) meta.push_back (p);
        }
        else if (! line.empty() && line.rfind ("//", 0) != 0)
        {
            size_t k = 0;
            while (k < line.size() && std::isdigit ((unsigned char) line[k])) ++k;
            if (bank && k > 0 && k < line.size())
            {
                std::string name = line.substr (k);
                name.erase (0, name.find_first_not_of (' '));
                if (prop (meta, "f").find ("hidden") == std::string::npos)
                {
                    int channel = -1;
                    const auto sends = reaticulateOutputs (prop (meta, "o"), channel);
                    bank->articulations.push_back (make (name, sends, channel, bank->articulations.size()));
                }
            }
            meta.clear();
        }
    }
    if (r.maps.empty()) r.error = "This isn't a Reaticulate bank file.";
    return r;
}
} // namespace

Imported import (const std::string& text, const std::string& filename)
{
    const auto f = lower (filename);
    auto endsWith = [&f] (const std::string& e) { return f.size() >= e.size() && f.compare (f.size() - e.size(), e.size(), e) == 0; };
    if (text.find ("<InstrumentMap") != std::string::npos || text.find ("<plist") != std::string::npos || endsWith (".plist"))
    {
        const auto parsed = xml::parse (text);
        if (! parsed.root) return { {}, {}, "The file couldn't be read: " + parsed.error };
        if (parsed.root->name == "InstrumentMap") return cubase (*parsed.root);
        return logic (*parsed.root);
    }
    if (endsWith (".reabank") || lower (text).find ("\nbank ") != std::string::npos || lower (text).rfind ("bank ", 0) == 0)
        return reaticulate (text);
    return { {}, {}, "Choose a Cubase expression map (.expressionmap), a Logic articulation set (.plist) or a Reaticulate bank (.reabank)." };
}

std::string describe (const Articulation& a)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    auto note = [] (int p) { return std::string (names[((p % 12) + 12) % 12]) + std::to_string (p / 12 - 1); };
    std::vector<std::string> parts;
    auto out = [&] (int kind, int number, int value)
    {
        if (kind == ArtOutput::Note) parts.push_back (note (number) + (value != 127 ? " vel " + std::to_string (value) : ""));
        else if (kind == ArtOutput::Controller) parts.push_back ("CC" + std::to_string (number) + " = " + std::to_string (value));
        else parts.push_back ("Program " + std::to_string (number));
    };
    if (a.keyswitch >= 0) out (ArtOutput::Note, a.keyswitch, a.keyswitchVelocity);
    for (auto& o : a.more) if (o.kind == ArtOutput::Note) out (o.kind, o.number, o.value);
    if (a.cc >= 0) out (ArtOutput::Controller, a.cc, a.ccValue);
    for (auto& o : a.more) if (o.kind == ArtOutput::Controller) out (o.kind, o.number, o.value);
    if (a.program >= 0) out (ArtOutput::Program, a.program, 0);
    for (auto& o : a.more) if (o.kind == ArtOutput::Program) out (o.kind, o.number, o.value);
    std::string s;
    for (size_t i = 0; i < parts.size(); ++i) s += (i ? " + " : "") + parts[i];
    if (a.channel >= 0) s += (s.empty() ? "" : ", ") + std::string ("channel ") + std::to_string (a.channel + 1);
    return s.empty() ? "nothing" : s;
}

std::string saveLibrary (const std::vector<ArticulationMap>& maps) { return file::saveMaps (maps); }
std::vector<ArticulationMap> loadLibrary (const std::string& text) { return file::loadMaps (text); }

} // namespace kal::articulations
