#include "DrumSounds.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace kal::drumsound
{
namespace
{
// Words, lower case: split at anything not a letter or digit, where a small
// letter meets a capital ("SnareTight"), and where letters meet digits ("BD808").
std::vector<std::string> wordsOf (const std::string& s)
{
    std::vector<std::string> out;
    std::string w;
    auto kind = [] (char c) { return std::isdigit ((unsigned char) c) ? 1 : std::isalpha ((unsigned char) c) ? 2 : 0; };
    for (size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];
        const bool split = kind (c) == 0 || (! w.empty() && kind (c) != kind (s[i - 1]))
                        || (! w.empty() && std::isupper ((unsigned char) c) && std::islower ((unsigned char) s[i - 1]));
        if (split && ! w.empty()) { out.push_back (w); w.clear(); }
        if (kind (c) != 0) w += (char) std::tolower ((unsigned char) c);
    }
    if (! w.empty()) out.push_back (w);
    return out;
}

bool hasWord (const std::vector<std::string>& ws, std::initializer_list<const char*> any)
{
    for (auto& w : ws) for (auto* a : any) if (w == a) return true;
    return false;
}

bool startsWord (const std::vector<std::string>& ws, std::initializer_list<const char*> any)
{
    for (auto& w : ws) for (auto* a : any) if (w.rfind (a, 0) == 0) return true;
    return false;
}

Kind kindOfWords (const std::vector<std::string>& ws)
{
    const bool hat = startsWord (ws, { "hat", "hihat", "hh" }) || (hasWord (ws, { "hi" }) && hasWord (ws, { "hat", "hats" }));
    if (hasWord (ws, { "ohh", "oh" }) || (hat && hasWord (ws, { "open", "op", "o" }))) return Kind::OpenHat;
    if (hasWord (ws, { "chh", "ch" }) || (hat && hasWord (ws, { "closed", "close", "cl", "c" }))) return Kind::ClosedHat;
    if (hat) return Kind::Hat;
    if (startsWord (ws, { "kick", "kik", "bassdrum" }) || hasWord (ws, { "bd", "kck" }) || (hasWord (ws, { "bass" }) && hasWord (ws, { "drum" }))) return Kind::Kick;
    if (startsWord (ws, { "snare", "snr" }) || hasWord (ws, { "sd", "sn" })) return Kind::Snare;
    if (startsWord (ws, { "clap", "clp" })) return Kind::Clap;
    if (startsWord (ws, { "rim", "sidestick" }) || hasWord (ws, { "stick", "rs" })) return Kind::Rim;
    if (startsWord (ws, { "tom" })) return Kind::Tom;
    if (startsWord (ws, { "crash" })) return Kind::Crash;
    if (startsWord (ws, { "ride" })) return Kind::Ride;
    if (startsWord (ws, { "cymbal", "cym", "splash", "china" })) return Kind::Cymbal;
    if (startsWord (ws, { "shaker", "shake", "tamb" })) return Kind::Shaker;
    if (startsWord (ws, { "perc", "conga", "bongo", "cowbell", "clave", "block", "drum" })) return Kind::Percussion;
    return Kind::Other;
}

// A file's path in parts: its name (without the extension) last.
std::vector<std::string> partsOf (const std::string& path)
{
    std::vector<std::string> parts;
    std::string part;
    for (char c : path)
    {
        if (c == '/' || c == '\\') { if (! part.empty()) parts.push_back (part); part.clear(); }
        else part += c;
    }
    if (! part.empty()) parts.push_back (part);
    if (! parts.empty())
        if (auto dot = parts.back().rfind ('.'); dot != std::string::npos && dot > 0) parts.back() = parts.back().substr (0, dot);
    return parts;
}

// Words in a file's name that say it isn't a drum.
bool notADrum (const std::vector<std::string>& ws)
{
    return startsWord (ws, { "pad", "vocal", "vox", "voice", "chord", "piano", "guitar", "gtr", "synth", "string", "riser", "sweep",
                             "uplift", "downlift", "drone", "atmos", "ambien", "texture", "melod", "riff", "arp", "choir", "brass",
                             "sax", "flute", "loop", "fill", "groove", "break", "beat", "phrase", "song" })
        || hasWord (ws, { "keys", "lead", "fx", "sfx", "bpm" })
        || (hasWord (ws, { "bass" }) && ! hasWord (ws, { "drum" }));   // a bass, not a bass drum
}

double kindMaxSeconds (Kind k) { return k == Kind::Crash || k == Kind::Ride || k == Kind::Cymbal ? 8.0 : 3.0; }

// Its shape: heard by the scan, or failing that from its waveform's columns.
struct Shape { bool known = false; double onsetMs = 0, attackMs = 0, tailDb = 0, zcr = -1; };
Shape shapeOf (const library::Item& it)
{
    Shape s;
    if (it.onsetMs >= 0)
    {
        s.known = true;
        s.onsetMs = it.onsetMs;
        s.attackMs = it.attackMs;
        s.tailDb = it.tailDb;
        s.zcr = it.zcr;
        return s;
    }
    if (it.peaks.empty() || it.seconds <= 0) return s;
    const auto top = *std::max_element (it.peaks.begin(), it.peaks.end());
    if (top == 0) return s;
    const double colMs = it.seconds * 1000.0 / (double) it.peaks.size();
    size_t onset = 0;
    while (onset < it.peaks.size() && it.peaks[onset] < top / 10) ++onset;
    size_t risen = onset;
    while (risen < it.peaks.size() && it.peaks[risen] < top * 9 / 10) ++risen;
    const size_t tailFrom = it.peaks.size() - std::max<size_t> (1, it.peaks.size() / 10);
    double sum = 0;
    for (size_t i = tailFrom; i < it.peaks.size(); ++i) sum += it.peaks[i];
    const double tail = sum / (double) (it.peaks.size() - tailFrom);
    s.known = true;
    s.onsetMs = (double) onset * colMs;
    s.attackMs = std::max (0.0, ((double) risen - (double) onset - 1.0) * colMs);   // a column's width is uncertainty, not rise
    s.tailDb = tail > 0 ? 20.0 * std::log10 (tail / top) : -120.0;
    return s;
}

bool hitShaped (const Shape& s) { return s.known && s.onsetMs <= 60 && s.attackMs <= 20 && s.tailDb <= -12; }

// A drum's kind from how it sounds, when its name says nothing.
Kind heardKind (const library::Item& it, const Shape& s)
{
    if (s.zcr < 0) return Kind::Percussion;
    if (s.zcr < 800 && (it.note < 0 || it.note <= 43)) return Kind::Kick;
    if (s.zcr < 2500 && it.note >= 40 && it.note <= 64) return Kind::Tom;
    if (s.zcr >= 9000) return it.seconds < 0.35 ? Kind::ClosedHat : it.seconds < 1.2 ? Kind::OpenHat : Kind::Cymbal;
    if (s.zcr >= 2500) return it.seconds < 0.7 ? Kind::Snare : Kind::Percussion;
    return Kind::Percussion;
}
} // namespace

Kind kindOf (const std::string& s)
{
    if (s.find ('/') == std::string::npos && s.find ('\\') == std::string::npos) return kindOfWords (wordsOf (s));
    const auto parts = partsOf (s);
    for (int i = (int) parts.size() - 1, depth = 0; i >= 0 && depth < 4; --i, ++depth)
        if (const auto k = kindOfWords (wordsOf (parts[(size_t) i])); k != Kind::Other) return k;
    return Kind::Other;
}

std::string nameOf (Kind k)
{
    switch (k)
    {
        case Kind::Kick: return "Kick";
        case Kind::Snare: return "Snare";
        case Kind::Clap: return "Clap";
        case Kind::ClosedHat: return "Closed Hat";
        case Kind::OpenHat: return "Open Hat";
        case Kind::Hat: return "Hat";
        case Kind::Tom: return "Tom";
        case Kind::Crash: return "Crash";
        case Kind::Ride: return "Ride";
        case Kind::Cymbal: return "Cymbal";
        case Kind::Rim: return "Rim";
        case Kind::Shaker: return "Shaker";
        case Kind::Percussion: return "Percussion";
        default: return "Drum";
    }
}

const std::vector<Kind>& kinds()
{
    static const std::vector<Kind> k { Kind::Kick, Kind::Snare, Kind::Clap, Kind::Hat, Kind::Tom, Kind::Cymbal, Kind::Rim, Kind::Shaker, Kind::Percussion };
    return k;
}

Verdict Cache::of (const library::Item& it)
{
    auto& e = entries[it.path];
    if (e.modified != it.modified || e.size != it.size || e.analysis != it.analysis || e.modified == 0)
    {
        e.modified = it.modified;
        e.size = it.size;
        e.analysis = it.analysis;
        e.v = drumsound::of (it);
    }
    return e.v;
}

bool fits (Kind want, Kind got)
{
    if (want == got) return true;
    switch (want)
    {
        case Kind::Hat: return got == Kind::ClosedHat || got == Kind::OpenHat;
        case Kind::ClosedHat: case Kind::OpenHat: return got == Kind::Hat;
        case Kind::Cymbal: return got == Kind::Crash || got == Kind::Ride;
        case Kind::Crash: case Kind::Ride: return got == Kind::Cymbal;
        default: return false;
    }
}

Verdict of (const library::Item& it)
{
    Verdict v;
    if (it.kind != library::Kind::Audio || ! it.readable || it.loop || it.seconds <= 0.005) return v;
    const auto parts = partsOf (it.path);
    if (parts.empty()) return v;
    const auto fileWords = wordsOf (parts.back());
    if (library::tempoFromName (parts.back()) > 0) return v;   // a tempo in its name: a loop
    const auto shape = shapeOf (it);
    const Kind byFile = kindOfWords (fileWords);
    if (byFile != Kind::Other && ! notADrum (fileWords))
    {
        // Named as a drum: its length, and not rising slowly.
        if (it.seconds > kindMaxSeconds (byFile)) return v;
        if (shape.known && (shape.attackMs > 80 || shape.onsetMs > 200)) return v;
        return { true, byFile, false };
    }
    if (notADrum (fileWords) && byFile == Kind::Other) return v;
    if (byFile != Kind::Other) return v;   // a drum word, but with words that say it isn't one ("Snare Roll Riser")
    // Its folders, then: a hit's shape too.
    Kind byFolder = Kind::Other;
    for (int i = (int) parts.size() - 2, depth = 0; i >= 0 && depth < 3 && byFolder == Kind::Other; --i, ++depth)
    {
        const auto ws = wordsOf (parts[(size_t) i]);
        if (notADrum (ws)) return v;   // in a folder of loops or pads ("Drum Loops"): not a drum hit
        byFolder = kindOfWords (ws);
    }
    if (byFolder != Kind::Other)
    {
        if (it.seconds > kindMaxSeconds (byFolder) || (shape.known && ! hitShaped (shape))) return v;
        if (byFolder == Kind::Percussion && shape.known) return { true, heardKind (it, shape), true };   // "Drums/": which, heard
        return { true, byFolder, false };
    }
    // Nothing named: shaped like a hit and short, its kind heard.
    if (! hitShaped (shape) || it.seconds > 2.0) return v;
    return { true, heardKind (it, shape), true };
}

} // namespace kal::drumsound
