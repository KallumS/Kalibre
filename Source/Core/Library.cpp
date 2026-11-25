#include "Library.h"
#include "DrumSounds.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <istream>
#include <iterator>
#include <ostream>
#include <random>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace kal::library
{
using nlohmann::json;

namespace
{
std::string lower (std::string s)
{
    for (auto& ch : s) ch = (char) std::tolower ((unsigned char) ch);
    return s;
}

// A shuffle that's the same for the same seed: each path's place from a hash.
std::uint64_t mix (std::uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

std::uint64_t shuffleKey (const std::string& path, std::uint32_t seed)
{
    std::uint64_t h = 1469598103934665603ull;   // FNV-1a
    for (unsigned char ch : path) { h ^= ch; h *= 1099511628211ull; }
    return mix (h ^ ((std::uint64_t) seed << 32 | seed));
}

bool byName (const std::string& a, const std::string& b)
{
    const auto la = lower (nameOf (a)), lb = lower (nameOf (b));
    return la != lb ? la < lb : a < b;
}

// How long a sound is, in seconds: a sample's own, a MIDI file's beats at its tempo (or 120).
double lengthOf (const Item& it)
{
    if (it.kind == Kind::Midi) return it.beats * 60.0 / (it.bpm > 0 ? it.bpm : 120.0);
    return it.seconds;
}

void order (std::vector<std::string>& paths, const Settings& s, const Index& index)
{
    if (s.shuffled)
    {
        std::sort (paths.begin(), paths.end(), [&s] (const std::string& a, const std::string& b)
        {
            const auto ka = shuffleKey (a, s.seed), kb = shuffleKey (b, s.seed);
            return ka != kb ? ka < kb : a < b;
        });
        return;
    }
    using Sort = Settings::Sort;
    if (s.sort == Sort::AToZ) { std::sort (paths.begin(), paths.end(), byName); return; }
    if (s.sort == Sort::ZToA) { std::sort (paths.begin(), paths.end(), [] (const std::string& a, const std::string& b) { return byName (b, a); }); return; }
    // By length or tempo, the same ones by name; a sound whose tempo isn't known last.
    auto item = [&index] (const std::string& p) -> const Item* { auto it = index.items.find (p); return it != index.items.end() ? &it->second : nullptr; };
    std::stable_sort (paths.begin(), paths.end(), [&] (const std::string& a, const std::string& b)
    {
        const auto* ia = item (a);
        const auto* ib = item (b);
        double va = 0, vb = 0;
        bool ka = ia != nullptr, kb = ib != nullptr;
        const bool byTempo = s.sort == Sort::SlowestFirst || s.sort == Sort::FastestFirst;
        if (ia) { va = byTempo ? ia->bpm : lengthOf (*ia); ka = ! byTempo || ia->bpm > 0; }
        if (ib) { vb = byTempo ? ib->bpm : lengthOf (*ib); kb = ! byTempo || ib->bpm > 0; }
        if (ka != kb) return ka;   // known before unknown
        if (ka && va != vb) return (s.sort == Sort::LongestFirst || s.sort == Sort::FastestFirst) ? va > vb : va < vb;
        return byName (a, b);
    });
}

// Folders are parted by '/', and on Windows by '\\' too (0129).
bool isSlash (char c) { return c == '/' || c == '\\'; }

std::string trimSlash (std::string s)
{
    while (s.size() > 1 && isSlash (s.back())) s.pop_back();
    return s;
}

const char* hex = "0123456789abcdef";
} // namespace

//==============================================================================
bool isMidiExtension (const std::string& ext)
{
    const auto e = lower (ext);
    return e == ".mid" || e == ".midi";
}

std::string extensionOf (const std::string& path)
{
    const auto name = nameOf (path);
    const auto dot = name.find_last_of ('.');
    return dot == std::string::npos || dot == 0 ? std::string() : lower (name.substr (dot));
}

std::string nameOf (const std::string& path)
{
    const auto p = trimSlash (path);
    const auto slash = p.find_last_of ("/\\");
    return slash == std::string::npos ? p : p.substr (slash + 1);
}

std::string parentOf (const std::string& path)
{
    const auto p = trimSlash (path);
    const auto slash = p.find_last_of ("/\\");
    if (slash == std::string::npos) return {};
    return slash == 0 ? p.substr (0, 1) : p.substr (0, slash);
}

bool isInside (const std::string& path, const std::string& folder)
{
    const auto f = trimSlash (folder);
    if (f.empty() || path.size() <= f.size()) return false;
    if (path.compare (0, f.size(), f) != 0) return false;
    return f == "/" || f == "\\" || isSlash (path[f.size()]);
}

double tempoFromName (const std::string& name)
{
    const auto s = lower (name);
    auto isNum = [] (char ch) { return std::isdigit ((unsigned char) ch) || ch == '.'; };
    for (std::size_t at = s.find ("bpm"); at != std::string::npos; at = s.find ("bpm", at + 1))
    {
        // The number before it ("96bpm", "96 bpm", "96_bpm")...
        std::size_t end = at;
        while (end > 0 && (s[end - 1] == ' ' || s[end - 1] == '_' || s[end - 1] == '-')) --end;
        std::size_t start = end;
        while (start > 0 && isNum (s[start - 1])) --start;
        double bpm = start < end ? std::atof (s.substr (start, end - start).c_str()) : 0;
        // ...or after it ("bpm120", "BPM_120").
        if (bpm <= 0)
        {
            std::size_t b = at + 3;
            while (b < s.size() && (s[b] == ' ' || s[b] == '_' || s[b] == '-')) ++b;
            std::size_t e = b;
            while (e < s.size() && isNum (s[e])) ++e;
            if (e > b) bpm = std::atof (s.substr (b, e - b).c_str());
        }
        if (bpm >= 40 && bpm <= 300) return bpm;
    }
    return 0;
}

bool needsScan (const Item* cached, std::int64_t size, std::int64_t modified)
{
    return cached == nullptr || cached->size != size || cached->modified != modified
           || (cached->kind == Kind::Audio && cached->readable && cached->analysis < analysisVersion);
}

std::string randomSound (const Index& index, unsigned seed, const std::string& avoid)
{
    // Reservoir sampling: the n-th fitting sound replaces the choice with a chance of 1 in n.
    std::mt19937 rng (seed);
    std::string chosen;
    std::uint64_t n = 0;
    for (auto& [path, it] : index.items)
    {
        if (it.kind != Kind::Audio || ! it.readable || it.seconds <= 0 || it.seconds > 600 || path == avoid) continue;
        ++n;
        if (std::uniform_int_distribution<std::uint64_t> (0, n - 1) (rng) == 0) chosen = path;
    }
    return chosen;
}

std::string noteName (int note, double cents)
{
    if (note < 0 || note > 127) return {};
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    std::string s = std::string (names[note % 12]) + std::to_string (note / 12 - 1);   // middle C (60) is C4, as everywhere in Kalibre
    const int c = (int) std::lround (cents);
    if (c != 0) s += (c > 0 ? " +" : " ") + std::to_string (c);
    return s;
}

std::string loudnessText (const Item& it)
{
    char buf[48];
    if (it.lufs > -70) { std::snprintf (buf, sizeof buf, "%.1f LUFS", it.lufs); return buf; }
    if (it.peakDb > -90) { std::snprintf (buf, sizeof buf, "Peak %.1f dB", it.peakDb); return buf; }
    return {};
}

bool isFavourite (const Settings& s, const std::string& path)
{
    if (s.favourites.empty()) return false;
    for (std::string p = trimSlash (path); ! p.empty(); )
    {
        if (s.favourites.count (p) > 0) return true;
        const auto up = parentOf (p);
        if (up == p) break;
        p = up;
    }
    return false;
}

//==============================================================================
std::vector<Row> rows (const Index& index, const Settings& s, const std::string& filter, drumsound::Cache* drums)
{
    drumsound::Cache own;
    if (drums == nullptr) drums = &own;
    const auto want = lower (filter);
    std::vector<std::string> roots;
    for (auto& f : s.folders) roots.push_back (trimSlash (f));

    // Each shown item, under the deepest chosen folder that holds it.
    std::map<std::string, std::vector<std::string>> byRoot;
    for (auto& [path, item] : index.items)
    {
        if (! item.readable) continue;
        if (! want.empty() && lower (nameOf (path)).find (want) == std::string::npos) continue;
        if (s.favouritesOnly && ! isFavourite (s, path)) continue;
        if (s.drumsOnly)
        {
            const auto v = drums->of (item);
            if (! v.drum) continue;
            if (s.drumKind >= 0 && ! drumsound::fits ((drumsound::Kind) s.drumKind, v.kind)) continue;
        }
        const std::string* root = nullptr;
        for (auto& r : roots)
            if (isInside (path, r) && (! root || r.size() > root->size())) root = &r;
        if (root) byRoot[*root].push_back (path);
    }

    std::vector<Row> out;
    if (s.flat)
    {
        std::vector<std::string> all;
        for (auto& [r, paths] : byRoot) all.insert (all.end(), paths.begin(), paths.end());
        order (all, s, index);
        for (auto& p : all) out.push_back ({ Row::Type::Item, p, 0, false, 0 });
        return out;
    }

    const bool openAll = ! want.empty() || s.favouritesOnly || s.drumsOnly;
    for (auto& root : roots)
    {
        const auto found = byRoot.find (root);
        const auto& paths = found != byRoot.end() ? found->second : std::vector<std::string>();
        if (paths.empty() && openAll) continue;
        // Folder -> its items directly, its folders, and how many items in all.
        std::map<std::string, std::vector<std::string>> itemsIn;
        std::map<std::string, std::set<std::string>> foldersIn;
        std::map<std::string, int> counts;
        for (auto& p : paths)
        {
            std::string folder = parentOf (p);
            itemsIn[folder].push_back (p);
            for (std::string f = folder; ; )
            {
                ++counts[f];
                if (f == root) break;
                const auto up = parentOf (f);
                foldersIn[up].insert (f);
                f = up;
            }
        }
        std::function<void (const std::string&, int)> add = [&] (const std::string& folder, int depth)
        {
            const bool open = openAll || s.open.count (folder) > 0;
            out.push_back ({ Row::Type::Folder, folder, depth, open, counts[folder] });
            if (! open) return;
            std::vector<std::string> sub (foldersIn[folder].begin(), foldersIn[folder].end());
            std::sort (sub.begin(), sub.end(), byName);
            if (! s.shuffled && s.sort == Settings::Sort::ZToA) std::reverse (sub.begin(), sub.end());
            for (auto& f : sub) add (f, depth + 1);
            auto items = itemsIn[folder];
            order (items, s, index);
            for (auto& p : items) out.push_back ({ Row::Type::Item, p, depth + 1, false, 0 });
        };
        add (root, 0);
    }
    return out;
}

std::vector<std::string> itemsInOrder (const std::vector<Row>& rs)
{
    std::vector<std::string> out;
    for (auto& r : rs) if (r.type == Row::Type::Item) out.push_back (r.path);
    return out;
}

std::vector<std::uint8_t> peaksFrom (const std::vector<float>& loudest)
{
    std::vector<std::uint8_t> out ((std::size_t) peakColumns, 0);
    if (loudest.empty()) return out;
    const double per = (double) loudest.size() / peakColumns;
    for (int c = 0; c < peakColumns; ++c)
    {
        const auto a = (std::size_t) std::floor (c * per);
        const auto b = std::max (a + 1, (std::size_t) std::floor ((c + 1) * per));
        float m = 0;
        for (auto i = a; i < b && i < loudest.size(); ++i) m = std::max (m, std::abs (loudest[i]));
        out[(std::size_t) c] = (std::uint8_t) std::lround (std::clamp (m, 0.0f, 1.0f) * 255.0f);
    }
    return out;
}

//==============================================================================
std::string toJson (const Settings& s)
{
    json j;
    j["folders"] = s.folders;
    j["asked"] = s.asked;
    j["favourites"] = std::vector<std::string> (s.favourites.begin(), s.favourites.end());
    j["flat"] = s.flat;
    j["favouritesOnly"] = s.favouritesOnly;
    j["shuffled"] = s.shuffled;
    j["sort"] = (int) s.sort;
    j["seed"] = s.seed;
    j["sync"] = s.sync;
    j["autoPlay"] = s.autoPlay;
    j["drumsOnly"] = s.drumsOnly;
    j["drumKind"] = s.drumKind;
    j["open"] = std::vector<std::string> (s.open.begin(), s.open.end());
    return j.dump (1);
}

bool fromJson (const std::string& text, Settings& s)
{
    const auto j = json::parse (text, nullptr, false);
    if (! j.is_object()) return false;
    Settings d;
    d.folders = j.value ("folders", std::vector<std::string>());
    d.asked = j.value ("asked", false);
    for (auto& f : j.value ("favourites", std::vector<std::string>())) d.favourites.insert (f);
    d.flat = j.value ("flat", false);
    d.favouritesOnly = j.value ("favouritesOnly", false);
    d.shuffled = j.value ("shuffled", false);
    d.sort = (Settings::Sort) std::clamp (j.value ("sort", 0), 0, 5);
    d.seed = j.value ("seed", (std::uint32_t) 1);
    d.sync = j.value ("sync", true);
    d.autoPlay = j.value ("autoPlay", true);
    d.drumsOnly = j.value ("drumsOnly", false);
    d.drumKind = j.value ("drumKind", -1);
    for (auto& f : j.value ("open", std::vector<std::string>())) d.open.insert (f);
    s = d;
    return true;
}

namespace
{
json itemJson (const std::string& path, const Item& it)
{
    json j;
    j["path"] = path;
    j["midi"] = it.kind == Kind::Midi;
    j["size"] = it.size;
    j["modified"] = it.modified;
    j["readable"] = it.readable;
    j["bpm"] = it.bpm;
    j["bpmFrom"] = (int) it.bpmFrom;
    j["loop"] = it.loop;
    if (it.kind == Kind::Audio)
    {
        j["lufs"] = it.lufs;
        j["peakDb"] = it.peakDb;
        j["note"] = it.note;
        j["cents"] = it.cents;
        j["analysis"] = it.analysis;
        if (it.onsetMs >= 0) { j["onset"] = it.onsetMs; j["attack"] = it.attackMs; j["tail"] = it.tailDb; j["zcr"] = it.zcr; }
        j["seconds"] = it.seconds;
        j["rate"] = it.sampleRate;
        j["channels"] = it.channels;
        std::string p;
        for (auto v : it.peaks) { p += hex[v >> 4]; p += hex[v & 15]; }
        j["peaks"] = p;
    }
    else
    {
        j["beats"] = it.beats;
        json notes = json::array();
        for (auto& n : it.notes) notes.push_back ({ n.start, n.length, n.pitch });
        j["notes"] = notes;
    }
    return j;
}

// False when it isn't a sound's entry.
bool itemFrom (const json& e, Item& it)
{
    if (! e.is_object() || ! e.contains ("path")) return false;
    it.path = e.value ("path", std::string());
    it.kind = e.value ("midi", false) ? Kind::Midi : Kind::Audio;
    it.size = e.value ("size", (std::int64_t) 0);
    it.modified = e.value ("modified", (std::int64_t) 0);
    it.readable = e.value ("readable", true);
    it.bpm = e.value ("bpm", 0.0);
    it.bpmFrom = (Item::TempoFrom) std::clamp (e.value ("bpmFrom", it.bpm > 0 ? 1 : 0), 0, 3);
    it.loop = e.value ("loop", false);
    it.lufs = e.value ("lufs", silentDb);
    it.peakDb = e.value ("peakDb", silentDb);
    it.note = e.value ("note", -1);
    it.cents = e.value ("cents", 0.0);
    it.analysis = e.value ("analysis", 0);
    it.onsetMs = e.value ("onset", -1.0);
    it.attackMs = e.value ("attack", -1.0);
    it.tailDb = e.value ("tail", 0.0);
    it.zcr = e.value ("zcr", -1.0);
    it.seconds = e.value ("seconds", 0.0);
    it.sampleRate = e.value ("rate", 0.0);
    it.channels = e.value ("channels", 0);
    const auto p = e.value ("peaks", std::string());
    auto nib = [] (char ch) { return ch <= '9' ? ch - '0' : ch - 'a' + 10; };
    for (std::size_t i = 0; i + 1 < p.size(); i += 2) it.peaks.push_back ((std::uint8_t) (nib (p[i]) * 16 + nib (p[i + 1])));
    it.beats = e.value ("beats", 0.0);
    if (e.contains ("notes") && e["notes"].is_array())
        for (auto& n : e["notes"])
            if (n.is_array() && n.size() == 3) it.notes.push_back ({ n[0].get<float>(), n[1].get<float>(), n[2].get<int>() });
    return true;
}
} // namespace

std::string toJson (const Index& index)
{
    json items = json::array();
    for (auto& [path, it] : index.items) items.push_back (itemJson (path, it));
    return json ({ { "version", 1 }, { "items", items } }).dump();
}

bool fromJson (const std::string& text, Index& index)
{
    const auto j = json::parse (text, nullptr, false);
    if (! j.is_object() || ! j.contains ("items") || ! j["items"].is_array()) return false;
    Index d;
    for (auto& e : j["items"])
    {
        Item it;
        if (itemFrom (e, it)) d.items[it.path] = std::move (it);
    }
    index = std::move (d);
    return true;
}

// The index a sound a line (0106): half a million sounds written and read
// without the whole of it held as one JSON document.
bool writeIndex (const Index& index, std::ostream& out)
{
    out << json ({ { "kalibreSounds", 2 }, { "items", index.items.size() } }).dump() << '\n';
    for (auto& [path, it] : index.items) out << itemJson (path, it).dump() << '\n';
    return (bool) out;
}

bool readIndex (std::istream& in, Index& index)
{
    std::string first;
    if (! std::getline (in, first)) return false;
    const auto head = json::parse (first, nullptr, false);
    if (! head.is_object() || ! head.contains ("kalibreSounds"))
    {
        // The whole document, as Kalibre wrote it before.
        std::string rest ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        return fromJson (first + "\n" + rest, index);
    }
    Index d;
    std::string line;
    while (std::getline (in, line))
    {
        if (line.empty()) continue;
        const auto e = json::parse (line, nullptr, false);
        Item it;
        if (itemFrom (e, it)) d.items[it.path] = std::move (it);
    }
    index = std::move (d);
    return true;
}

} // namespace kal::library
