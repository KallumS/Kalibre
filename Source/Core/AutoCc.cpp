#include "AutoCc.h"

#include "Edit.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace kal::autocc
{

std::string familyName (Family f)
{
    switch (f)
    {
        case Family::Strings:   return "strings";
        case Family::Brass:     return "brass";
        case Family::Woodwinds: return "woodwinds";
        case Family::Ensemble:  return "the whole orchestra";
        default:                return {};
    }
}

//==============================================================================
// Names (decision 0060). AutoCC's presets are by section, so every instrument
// of a section counts, written as track names write them: singular or
// plural, numbered (Violins 1, Vln II, 1st Violins, V1), abbreviated, in
// Italian, French or German, with accents or without, run together or
// spaced out letter by letter. AutoCC's own study (tools/ccstudy.py) sorts
// tracks by such words; its loosest (bass, legato, long) are left out here,
// where "Synth Bass" must not count as strings.
namespace
{
// Accented letters as plain ones: "Hörner" is "horner", "Flûte" "flute".
std::string plain (const std::string& in)
{
    static const char* from[] { "\xc3\xa0", "\xc3\xa1", "\xc3\xa2", "\xc3\xa4", "\xc3\xa7", "\xc3\xa8", "\xc3\xa9", "\xc3\xaa", "\xc3\xab",
                                "\xc3\xac", "\xc3\xad", "\xc3\xae", "\xc3\xaf", "\xc3\xb2", "\xc3\xb3", "\xc3\xb4", "\xc3\xb6", "\xc3\xb9",
                                "\xc3\xba", "\xc3\xbb", "\xc3\xbc", "\xc3\x9f", "\xc3\x84", "\xc3\x96", "\xc3\x9c", "\xc3\x89" };
    static const char* to[] { "a", "a", "a", "a", "c", "e", "e", "e", "e", "i", "i", "i", "i", "o", "o", "o", "o", "u", "u", "u", "u", "ss",
                              "a", "o", "u", "e" };
    std::string out;
    for (size_t i = 0; i < in.size();)
    {
        bool done = false;
        for (size_t k = 0; k < sizeof (from) / sizeof (from[0]) && ! done; ++k)
            if (in.compare (i, 2, from[k]) == 0) { out += to[k]; i += 2; done = true; }
        if (! done) out += in[i++];
    }
    return out;
}

// Lower case words; letters and digits apart ("Violins1" is "violins 1");
// "V i o l i n s" joined back into "violins".
std::vector<std::string> wordsOf (const std::string& name)
{
    std::string n;
    char prev = ' ';
    for (char ch : plain (name))
    {
        const char c = std::isalnum ((unsigned char) ch) ? (char) std::tolower ((unsigned char) ch) : ' ';
        if (c != ' ' && prev != ' ' && (std::isdigit ((unsigned char) c) != 0) != (std::isdigit ((unsigned char) prev) != 0)) n += ' ';
        n += c;
        prev = c;
    }
    std::vector<std::string> raw;
    for (size_t i = 0; i < n.size();)
    {
        while (i < n.size() && n[i] == ' ') ++i;
        size_t j = i;
        while (j < n.size() && n[j] != ' ') ++j;
        if (j > i) raw.push_back (n.substr (i, j - i));
        i = j;
    }
    std::vector<std::string> words;
    for (size_t i = 0; i < raw.size();)
    {
        // Three or more single letters in a row are one word spelt out.
        size_t j = i;
        while (j < raw.size() && raw[j].size() == 1 && std::isalpha ((unsigned char) raw[j][0])) ++j;
        if (j - i >= 3) { std::string w; for (size_t k = i; k < j; ++k) w += raw[k]; words.push_back (w); i = j; }
        else words.push_back (raw[i++]);
    }
    return words;
}

// The singular: "violins" is "violin", "basses" "bass"; "brass" stays.
std::string singular (const std::string& w)
{
    if (w.size() > 4 && w.compare (w.size() - 4, 4, "sses") == 0) return w.substr (0, w.size() - 2);
    if (w.size() >= 3 && w.back() == 's' && w[w.size() - 2] != 's') return w.substr (0, w.size() - 1);
    return w;
}

struct Section
{
    Family family;
    std::vector<std::string> phrases;   // two or three words, in the singular
    std::vector<std::string> words;     // one word, in the singular, or a plural that isn't English
};

const std::vector<Section>& sections()
{
    // Woodwinds before brass before strings, phrases before words: an English
    // horn is a woodwind, a piccolo trumpet brass, a bass clarinet a woodwind.
    static const std::vector<Section> list {
        { Family::Woodwinds,
          { "english horn", "cor anglai", "corno inglese", "corni inglesi", "cor anglais", "basset horn", "bass clarinet", "contrabass clarinet",
            "alto clarinet", "alto flute", "bass flute", "contra bassoon", "double bassoon", "alto sax", "tenor sax", "baritone sax",
            "soprano sax", "bass sax", "alto saxophone", "tenor saxophone", "baritone saxophone", "soprano saxophone", "tin whistle",
            "low whistle", "pan flute", "pan pipe", "oboe d amore", "wind section", "woodwind section", "eng horn", "wood wind",
            "bass cl", "c fag" },
          { "flute", "flauto", "flauti", "flote", "floten", "fl", "piccolo", "picc", "pic", "ottavino", "oboe", "oboi", "oboen", "hautboi",
            "hautbois", "ob", "englischhorn", "clarinet", "clarinetto", "clarinetti", "clarinette", "klarinette", "klarinetten", "clar", "cl",
            "clt", "bassoon", "fagotto", "fagotti", "fagott", "fagotte", "basson", "bsn", "fg", "contrabassoon", "contrafagotto",
            "kontrafagott", "cbsn", "cbn", "fag", "bn", "eh", "englishhorn", "altoflute", "bassclarinet", "bcl", "saxophone", "sax", "recorder", "fife", "whistle", "panpipe", "shakuhachi", "bansuri", "dizi", "xiao",
            "ney", "nay", "duduk", "ocarina", "bagpipe", "quena", "suling", "zurna", "shawm", "crumhorn", "woodwind", "wind", "ww" } },
        { Family::Brass,
          { "french horn", "horn in f", "piccolo trumpet", "bass trumpet", "bass trombone", "contrabass trombone", "alto trombone",
            "wagner tuba", "contrabass tuba", "bass tuba", "alto horn", "tenor horn", "baritone horn", "low brass", "brass section" },
          { "horn", "corno", "corni", "cor", "horner", "hn", "hrn", "fhn", "trumpet", "tromba", "trombe", "trompette", "trompete", "trompeten",
            "tpt", "trp", "tp", "trombone", "tromboni", "posaune", "posaunen", "tbn", "trb", "tbne", "bone", "tuba", "tuben", "cimbasso",
            "euphonium", "flugelhorn", "flugel", "cornet", "cornetto", "bugle", "mellophone", "sousaphone", "alphorn", "sackbut", "serpent",
            "ophicleide", "brass", "brs", "blech", "waldhorn", "clarino", "cornett", "corneta", "brasswind" } },
        { Family::Strings,
          { "double bass", "string bass", "contra bass", "upright bass", "viola da gamba", "string section", "string ensemble" },
          { "violin", "violini", "violino", "violinen", "violon", "geige", "geigen", "vln", "vn", "vl", "viola", "viole", "bratsche", "bratschen",
            "vla", "va", "cello", "celli", "violoncello", "violoncelli", "violoncelle", "vc", "vlc", "vcl", "contrabass", "contrabbasso",
            "contrabbassi", "contrebasse", "contrebass", "kontrabass", "kontrabasse", "basses", "bass", "cb", "octobass", "viol", "violone", "fiddle", "erhu",
            "nyckelharpa", "sarangi", "kamancheh", "kemenche", "strings", "string", "stringed", "str", "stg", "strg", "doublebass", "db" } },
        { Family::Ensemble, { "full orchestra", "symphony orchestra" }, { "orchestra", "orchestral", "tutti", "orchester", "orchestre" } },
    };
    return list;
}
} // namespace

Family familyFor (const std::string& name)
{
    const auto words = wordsOf (name);
    std::vector<std::string> one;   // each word in the singular
    for (auto& w : words) one.push_back (singular (w));
    std::string spaced = " ";
    for (auto& w : one) spaced += w + " ";

    // "Bass" alone is too often a synth or an electric one, "Wind" a sound:
    // only "Basses" (or "Double Bass") and "Winds" count.
    auto isWord = [] (const Section& s, const std::string& w, const std::string& asWritten)
    {
        if (w == "bass" && asWritten != "basses") return false;
        if (w == "wind" && asWritten != "winds") return false;
        return std::find (s.words.begin(), s.words.end(), w) != s.words.end();
    };
    for (auto& s : sections())
        for (auto& ph : s.phrases)
            if (spaced.find (" " + ph + " ") != std::string::npos) return s.family;
    // Voices, percussion, harp and keyboards have no AutoCC preset, though they
    // share words with ours: "Choir Basses", "Orchestra Bells" (Orchestration
    // Helper's names for them).
    static const std::vector<std::string> notOurs {
        "choir", "chorus", "choral", "voice", "vox", "vocal", "singer", "soprano", "mezzo", "contralto",
        "percussion", "drum", "timpani", "timpano", "kettledrum", "tympani", "timp", "snare", "cymbal", "piatti", "crotale", "triangle",
        "triangolo", "tambourine", "castanet", "gong", "tamtam", "glockenspiel", "campanelli", "bell", "chime", "campane", "celesta",
        "celeste", "xylophone", "marimba", "vibraphone", "cassa", "harp", "arpa", "piano", "pianoforte", "harpsichord", "cembalo",
        "keyboard", "organ" };
    for (auto& w : one)
        if (std::find (notOurs.begin(), notOurs.end(), w) != notOurs.end()) return Family::None;
    for (auto& s : sections())
        for (size_t i = 0; i < one.size(); ++i)
        {
            if (isWord (s, one[i], words[i]) || isWord (s, words[i], words[i])) return s.family;
            // A numeral stuck on the end: "VlnII", "Violini I" written "ViolinII".
            for (const char* roman : { "iii", "ii", "iv", "i" })
            {
                const std::string r (roman);
                if (words[i].size() > r.size() + 1 && words[i].compare (words[i].size() - r.size(), r.size(), r) == 0)
                    if (isWord (s, singular (words[i].substr (0, words[i].size() - r.size())), {})) return s.family;
            }
        }
    // "V1", "V2" at the start of a name: the violins, numbered.
    if (words.size() >= 2 && words[0] == "v" && std::isdigit ((unsigned char) words[1][0])) return Family::Strings;
    return Family::None;
}

Found familyFor (const Track& t)
{
    if (auto f = familyFor (t.name); f != Family::None) return { f, t.name };
    for (auto& d : t.devices)
        if (d.role == DeviceRole::Instrument)
            if (auto f = familyFor (d.plugin.name); f != Family::None) return { f, d.plugin.name };
    return {};
}

//==============================================================================
// AutoCC's presets (load_preset in AutoCC.jsfx): its four fixed lanes,
// fitted to Lorne Balfe's mock-ups.
std::array<Lane, 4> preset (Family f)
{
    auto L = [] (int cc, double fl, double pk, double su, double atk, double dcy, double rel, double vel, double ae, double re)
    {
        return Lane { cc, fl, pk, su, atk, dcy, rel, vel, ae, re };
    };
    switch (f)
    {
        case Family::Strings:
            return { L (1, 85, 116, 116, 2200, 0, 1000, 10, 0.66, 1), L (11, 94, 118, 118, 1800, 0, 1000, 0, 0.54, 1),
                     L (7, 94, 94, 94, 0, 0, 0, 0, 1, 1), L (21, 92, 92, 92, 0, 0, 0, 0, 1, 1) };
        case Family::Brass:
            return { L (1, 102, 109, 106, 1800, 200, 1000, 10, 0.22, 1), L (11, 78, 85, 82, 1800, 400, 1000, 0, 0.23, 1),
                     L (7, 94, 94, 94, 0, 0, 0, 0, 1, 1), L (21, 92, 92, 92, 0, 0, 0, 0, 1, 1) };
        case Family::Woodwinds:
            return { L (1, 46, 63, 63, 800, 0, 1000, 10, 0.66, 1), L (11, 69, 88, 84, 1200, 800, 1000, 0, 0.66, 1),
                     L (7, 94, 94, 94, 0, 0, 0, 0, 1, 1), L (21, 92, 92, 92, 0, 0, 0, 0, 1, 1) };
        default:
            return { L (1, 91, 115, 115, 2600, 0, 1000, 10, 0.54, 1), L (11, 85, 104, 104, 2200, 0, 1000, 0, 0.47, 1),
                     L (7, 94, 94, 94, 0, 0, 0, 0, 1, 1), L (21, 92, 92, 92, 0, 0, 0, 0, 1, 1) };
    }
}

//==============================================================================
// The engine, as AutoCC.jsfx runs it: a step every 5 ms, each lane in one of
// five stages, a value sent only when its whole number changes.
namespace
{
constexpr int curvePoints = 33;            // AutoCC's curve tables
constexpr double restartGap = 0.4;         // seconds: a note this soon after the keys came up carries the line on
constexpr double rate = 48000.0;           // samples a second: AutoCC counts its steps in samples

struct Engine
{
    std::array<Lane, 4> lanes;
    Settings s;
    std::array<std::array<double, curvePoints>, 4> riseCurve {}, fallCurve {};
    struct State { int stage = 0; double t = 0, val = 0, start = 0, relst = 0, vsc = 1; int last = -1; };
    std::array<State, 4> st;
    std::array<bool, 128> held {};
    int count = 0;
    bool pedalDown = false;
    long long keyupAt = -1000000000LL;
    int channel = 0;
    long long pos = 0, phase = 0, step = 240;
    std::vector<std::pair<long long, MidiEvent>> out;   // at a sample

    Engine (Family f, const Settings& settings) : lanes (preset (f)), s (settings)
    {
        step = std::max<long long> (8, (long long) std::floor (rate * s.stepMs * 0.001));
        for (size_t i = 0; i < 4; ++i)
            for (int k = 0; k < curvePoints; ++k)
            {
                const double p = (double) k / (curvePoints - 1);
                riseCurve[i][(size_t) k] = std::pow (p, std::clamp (lanes[i].riseShape, 0.08, 8.0));
                fallCurve[i][(size_t) k] = std::pow (p, std::clamp (lanes[i].fallShape, 0.08, 8.0));
            }
        for (size_t i = 0; i < 4; ++i) st[i].val = st[i].start = st[i].relst = floorOf (i);
    }

    static double curveAt (const std::array<double, curvePoints>& c, double p)
    {
        p = std::clamp (p, 0.0, 1.0);
        const double x = p * (curvePoints - 1);
        const int i0 = (int) x;
        if (i0 >= curvePoints - 1) return c[curvePoints - 1];
        const double f = x - i0;
        return c[(size_t) i0] * (1 - f) + c[(size_t) i0 + 1] * f;
    }

    double dyn (size_t i) const { return i < 2 ? s.dynamic : 0; }
    double floorOf (size_t i) const { return std::clamp (lanes[i].floor + dyn (i), 0.0, 127.0); }
    double peakOf (size_t i) const { return std::clamp (lanes[i].peak + dyn (i), 0.0, 127.0); }
    double sustainOf (size_t i) const { return std::clamp (lanes[i].sustain + dyn (i), 0.0, 127.0); }

    void emit (size_t i, double v)
    {
        const int iv = std::clamp ((int) std::floor (v + 0.5), 0, 127);
        if (iv == st[i].last) return;
        st[i].last = iv;
        MidiEvent e;
        e.type = EventType::Controller;
        e.channel = channel;
        e.number = lanes[i].cc;
        e.value = iv;
        out.push_back ({ pos, e });
    }

    bool idle() const { for (auto& x : st) if (x.stage != 0) return false; return true; }

    void tick (double dt)
    {
        const int mode = (int) s.mode;
        for (size_t i = 0; i < 4; ++i)
        {
            auto& r = st[i];
            const auto& l = lanes[i];
            const double fl = floorOf (i);
            const double pk = fl + (peakOf (i) - fl) * r.vsc * s.depth;
            const double su = fl + (sustainOf (i) - fl) * r.vsc * s.depth;
            int stage = r.stage;
            double t = r.t + dt, v = r.val, dur = 0;
            for (bool done = false; ! done;)
            {
                if (stage == 1)
                {
                    dur = l.rise * 0.001 * s.time;
                    if (t >= dur) { t -= dur; stage = 2; }
                    else { v = r.start + (pk - r.start) * curveAt (riseCurve[i], t / dur); done = true; }
                }
                else if (stage == 2)
                {
                    dur = l.settle * 0.001 * s.time;
                    if (t >= dur) { t -= dur; if (mode == 2) { r.relst = su; stage = 4; } else stage = 3; }
                    else { v = pk + (su - pk) * (t / dur); done = true; }
                }
                else if (stage == 3)
                {
                    if (mode == 1)
                    {
                        dur = s.holdMs * 0.001 * s.time;
                        if (t >= dur) { t -= dur; r.relst = su; stage = 4; } else { v = su; done = true; }
                    }
                    else { v = su; done = true; }
                }
                else if (stage == 5)
                {
                    v = std::clamp (fl + r.relst, 0.0, 127.0);
                    if (mode != 0) { r.relst = v; stage = 4; t = 0; } else done = true;
                }
                else if (stage == 4)
                {
                    dur = l.fall * 0.001 * s.time;
                    if (t >= dur) { stage = 0; t = 0; }
                    else { v = r.relst + (fl - r.relst) * curveAt (fallCurve[i], t / dur); done = true; }
                }
                else { v = fl; t = 0; done = true; }
            }
            r.stage = stage;
            r.t = t;
            r.val = v;
            emit (i, v);
        }
    }

    void advance (long long target)
    {
        while (pos < target)
        {
            const long long n = std::min (step - phase, target - pos);
            pos += n;
            phase += n;
            if (phase >= step) { phase -= step; tick ((double) step / rate); }
        }
    }

    void startEnv (int velocity)
    {
        for (size_t i = 0; i < 4; ++i)
        {
            auto& r = st[i];
            r.start = r.stage == 0 ? floorOf (i) : r.val;
            r.val = r.start;
            r.stage = 1;
            r.t = 0;
            const double va = lanes[i].velocity * 0.01;
            r.vsc = 1 - va + va * (velocity / 127.0);
            emit (i, r.start);
        }
    }

    void carry()
    {
        for (size_t i = 0; i < 4; ++i)
            if (st[i].stage == 4) { st[i].relst = st[i].val - floorOf (i); st[i].stage = 5; st[i].t = 0; }
    }

    void release (bool force)
    {
        if (! force && s.mode != Settings::Mode::Gated) return;
        for (auto& r : st)
            if (r.stage != 0) { r.relst = r.val; r.stage = 4; r.t = 0; }
    }

    void noteOn (int pitch, int velocity, int ch)
    {
        channel = ch;
        if (! held[(size_t) pitch]) { held[(size_t) pitch] = true; ++count; }
        if (count == 1 || s.retrigger || idle())
        {
            if (count == 1 && ! s.retrigger && s.mode == Settings::Mode::Gated && ! idle() && (double) (pos - keyupAt) < restartGap * rate)
                carry();
            else
                startEnv (velocity);
        }
    }

    void noteOff (int pitch)
    {
        if (held[(size_t) pitch]) { held[(size_t) pitch] = false; count = std::max (0, count - 1); }
        if (count == 0) keyupAt = pos;
        if (count == 0 && ! (s.pedalHolds && pedalDown)) release (false);
    }

    void pedal (int value)
    {
        pedalDown = value >= 64;
        if (! pedalDown && count == 0) { keyupAt = pos; release (false); }
    }
};
} // namespace

std::vector<MidiEvent> render (const Clip& clip, const TempoMap& tempo, Family f, const Settings& settings)
{
    std::vector<MidiEvent> result;
    if (! clip.isMidi() || f == Family::None) return result;
    const Tick zero = clip.start - clip.offset;              // the clip's own tick 0, in the song
    const Tick from = clip.offset, to = clip.offset + clip.length;
    const double startSec = tempo.secondsAt (clip.start);
    auto sampleAt = [&] (Tick t) { return (long long) std::llround ((tempo.secondsAt (zero + t) - startSec) * rate); };

    // What AutoCC hears: the notes, and the sustain pedal. At the same moment
    // a key comes up before the next goes down, as a DAW sends them.
    struct In { long long at; int order; int a, b, ch; };
    std::vector<In> in;
    for (auto& n : clip.midi.notes)
    {
        if (n.start < from || n.start >= to) continue;
        in.push_back ({ sampleAt (n.start), 2, n.pitch, n.velocity, n.channel });
        in.push_back ({ sampleAt (std::min (n.end(), to)), 0, n.pitch, 0, n.channel });
    }
    for (auto& e : clip.midi.events)
        if (e.type == EventType::Controller && e.number == 64 && e.tick >= from && e.tick < to)
            in.push_back ({ sampleAt (e.tick), 1, 64, e.value, e.channel });
    std::stable_sort (in.begin(), in.end(), [] (const In& x, const In& y) { return x.at != y.at ? x.at < y.at : x.order < y.order; });

    Engine eng (f, settings);
    for (size_t i = 0; i < 4; ++i) eng.emit (i, eng.floorOf (i));   // the resting levels, from the start
    for (auto& e : in)
    {
        eng.advance (e.at);
        if (e.order == 2) eng.noteOn (std::clamp (e.a, 0, 127), e.b, e.ch);
        else if (e.order == 0) eng.noteOff (std::clamp (e.a, 0, 127));
        else eng.pedal (e.b);
    }
    // Let the last arc finish, as far as the clip goes.
    const long long end = sampleAt (to);
    while (! eng.idle() && eng.pos < end) eng.advance (std::min (end, eng.pos + eng.step));

    for (auto& [at, e] : eng.out)
    {
        auto ev = e;
        ev.tick = std::clamp<Tick> (tempo.tickAt (startSec + (double) at / rate) - zero, from, std::max<Tick> (from, to - 1));
        result.push_back (ev);
    }
    return result;
}

std::vector<int> fill (Project& p, Id clipId, Family f, const Settings& settings)
{
    Track* owner = nullptr;
    auto* clip = p.clip (clipId, &owner);
    if (! clip || ! clip->isMidi() || f == Family::None) return {};
    std::vector<int> ccs;
    for (auto& l : preset (f)) ccs.push_back (l.cc);
    auto& evs = clip->midi.events;
    evs.erase (std::remove_if (evs.begin(), evs.end(), [&] (const MidiEvent& e)
    {
        return e.type == EventType::Controller && std::find (ccs.begin(), ccs.end(), e.number) != ccs.end();
    }), evs.end());
    for (auto& e : render (*clip, p.tempo, f, settings)) evs.push_back (e);
    edit::sortNotes (clip->midi);
    if (owner)
        for (int cc : ccs)
            if (std::find (owner->ccLanes.begin(), owner->ccLanes.end(), cc) == owner->ccLanes.end()) owner->ccLanes.push_back (cc);
    return ccs;
}

} // namespace kal::autocc
