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
// Names. AutoCC's own study (tools/ccstudy.py) sorts tracks by such words; the
// loosest of its (bass, legato, long) are left out here, where a track named
// "Synth Bass" must not count as strings.
Family familyFor (const std::string& name)
{
    std::string n;
    for (char ch : name) n += std::isalnum ((unsigned char) ch) ? (char) std::tolower ((unsigned char) ch) : ' ';
    std::vector<std::string> words;
    for (size_t i = 0; i < n.size();)
    {
        while (i < n.size() && n[i] == ' ') ++i;
        size_t j = i;
        while (j < n.size() && n[j] != ' ') ++j;
        if (j > i) words.push_back (n.substr (i, j - i));
        i = j;
    }
    auto has = [&] (std::initializer_list<const char*> list)
    {
        for (auto& w : words) for (auto* x : list) if (w == x) return true;
        return false;
    };
    auto phrase = [&] (std::initializer_list<const char*> list)
    {
        const std::string spaced = " " + [&] { std::string s; for (auto& w : words) s += w + " "; return s; }();
        for (auto* x : list) if (spaced.find (" " + std::string (x) + " ") != std::string::npos) return true;
        return false;
    };
    // Two-word names first: an English horn is a woodwind, a bass trombone brass.
    if (phrase ({ "english horn", "cor anglais", "bass clarinet", "contrabass clarinet", "alto flute", "bass flute", "contra bassoon" }))
        return Family::Woodwinds;
    if (phrase ({ "french horn", "bass trombone", "wagner tuba", "contrabass trombone" })) return Family::Brass;
    if (phrase ({ "double bass", "double basses", "string bass", "contra bass", "string ensemble" })) return Family::Strings;
    if (has ({ "flute", "flutes", "fl", "fls", "piccolo", "picc", "oboe", "oboes", "ob", "clarinet", "clarinets", "clar", "cl", "cls",
               "bassoon", "bassoons", "bsn", "bsns", "contrabassoon", "recorder", "recorders", "woodwind", "woodwinds", "winds", "ww",
               "sax", "saxophone", "saxophones", "shakuhachi", "bansuri", "whistle", "duduk" }))
        return Family::Woodwinds;
    if (has ({ "horn", "horns", "hn", "hns", "fhn", "trumpet", "trumpets", "tpt", "tpts", "trp", "trombone", "trombones", "tbn",
               "tbns", "trb", "tuba", "tubas", "cimbasso", "cornet", "cornets", "flugelhorn", "flugel", "brass", "brs" }))
        return Family::Brass;
    if (has ({ "violin", "violins", "vln", "vlns", "vn", "vns", "viola", "violas", "vla", "vlas", "cello", "cellos", "celli", "vc",
               "vcs", "vlc", "contrabass", "contrabasses", "basses", "cb", "cbs", "strings", "string", "str", "fiddle" }))
        return Family::Strings;
    if (has ({ "orchestra", "orchestral", "tutti" })) return Family::Ensemble;
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
