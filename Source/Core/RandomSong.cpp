#include "RandomSong.h"

#include "Edit.h"
#include "Ideas.h"
#include "ScaleModel.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace kal::randomsong
{

bool isLoop (const Sample& s) { return s.bpm >= 60 && s.bpm <= 200 && s.seconds >= 1.0 && s.seconds <= 40.0; }

namespace
{
enum class Part { Drums, Bass, Chords, Lead, Counter, Pad, Loop };

const char* partName (Part p)
{
    switch (p)
    {
        case Part::Drums: return "Drums";
        case Part::Bass: return "Bass";
        case Part::Chords: return "Chords";
        case Part::Lead: return "Lead";
        case Part::Counter: return "Counter";
        case Part::Pad: return "Pad";
        default: return "Loop";
    }
}

// The roots' entries for the twelve pitch classes, spelled the usual way.
int rootIndexFor (int pc)
{
    static const int index[12] { 0, 1, 3, 5, 6, 7, 8, 10, 12, 13, 15, 16 };   // C C# D Eb E F F# G Ab A Bb B
    return index[(pc % 12 + 12) % 12];
}

std::vector<Note> repeated (const std::vector<Note>& notes, Tick every, Tick until)
{
    std::vector<Note> out;
    if (every <= 0) return out;
    for (Tick at = 0; at < until; at += every)
        for (auto n : notes)
        {
            n.id = 0;
            n.start += at;
            if (n.start >= until) continue;
            n.length = std::min (n.length, until - n.start);
            out.push_back (n);
        }
    return out;
}

template <typename T, typename R> const T* pick (const std::vector<T>& v, R& rng)
{
    if (v.empty()) return nullptr;
    return &v[std::uniform_int_distribution<size_t> (0, v.size() - 1) (rng)];
}
} // namespace

Result make (const Catalogue& cat, const Options& opt)
{
    std::mt19937 rng (opt.seed);
    auto chance = [&rng] (double p) { return std::uniform_real_distribution<double> (0, 1) (rng) < p; };
    auto between = [&rng] (int a, int b) { return std::uniform_int_distribution<int> (a, b) (rng); };

    Result r;
    Project& p = r.project;

    // The loops it may use, and the tempo: often a loop's own, so it isn't stretched far.
    std::vector<Sample> loops;
    for (auto& s : cat.samples) if (isLoop (s)) loops.push_back (s);
    double bpm = between (82, 148);
    if (! loops.empty() && chance (0.5)) bpm = std::round (pick (loops, rng)->bpm);
    p.tempo.setTempoAt (0, bpm);

    // The key: mostly major or natural minor, now and then a mode.
    const int pc = between (0, 11);
    static const int scaleChoices[] { 0, 0, 1, 1, 1, 4, 7 };   // Major, Minor (Natural), Dorian, Mixolydian
    p.scale.root = rootIndexFor (pc);
    p.scale.scale = scaleChoices[between (0, 6)];
    const int bars = 8;
    const Tick bar = PPQ * 4, length = bar * bars;
    p.loop.start = 0;
    p.loop.end = length;
    p.loop.on = true;

    // The music: Good Idea's tune and drums, Midi Suggester's harmony.
    const int seed = between (1, ideas::maxSeed);
    const auto tune = ideas::motif (p.scale, 4, 4, 4, seed);
    std::vector<Note> lead = tune.error.empty() ? repeated (tune.notes, std::max<Tick> (bar, tune.length), length) : std::vector<Note>();
    ideas::Harmony harmony;
    if (! lead.empty()) harmony = ideas::chordsFor (lead, length, 4, 4, between (1, 999));
    const bool hasHarmony = harmony.error.empty() && ! harmony.chords.empty();
    std::vector<Note> counter;
    if (hasHarmony)
        if (auto over = ideas::melodyOver (harmony.chords, length, 4, 4, between (1, 999)); over.error.empty()) counter = over.notes;
    const auto groove = ideas::drums (bars, 4, 4, between (1, ideas::maxSeed));

    // Which parts: drums, bass, chords and a lead first; then, by chance, more.
    std::vector<Part> parts;
    if (groove.error.empty() && ! groove.notes.empty()) parts.push_back (Part::Drums);
    if (hasHarmony) { parts.push_back (Part::Bass); parts.push_back (Part::Chords); }
    if (! lead.empty()) parts.push_back (Part::Lead);
    std::vector<Part> extras;
    if (! counter.empty()) extras.push_back (Part::Counter);
    if (hasHarmony) extras.push_back (Part::Pad);
    for (size_t i = 0; i < std::min<size_t> (2, loops.size()); ++i) extras.push_back (Part::Loop);
    std::shuffle (extras.begin(), extras.end(), rng);
    const int want = std::clamp (between (4, std::max (4, opt.maxTracks)), 1, std::max (1, opt.maxTracks));
    for (auto e : extras) if ((int) parts.size() < want) parts.push_back (e);

    // Instruments: drums to one that plays drums; the rest any but those.
    std::vector<Choice> tonal, drumKits;
    for (auto& c : cat.instruments) (c.drums ? drumKits : tonal).push_back (c);
    std::vector<Sample> drumHits, tonalHits;
    for (auto& s : cat.samples) if (! isLoop (s) && s.seconds > 0.02 && s.seconds < 8) (s.drums ? drumHits : tonalHits).push_back (s);

    int effectsLeft = opt.maxEffects, midiFxLeft = opt.maxMidiEffects;
    std::vector<Sample> loopsLeft = loops;
    std::shuffle (loopsLeft.begin(), loopsLeft.end(), rng);
    std::vector<std::string> names;

    for (auto part : parts)
    {
        std::string name = partName (part);
        const Sample* loop = nullptr;
        if (part == Part::Loop)
        {
            if (loopsLeft.empty()) continue;
            loop = &loopsLeft.back();
            name = loop->name.empty() ? "Loop" : loop->name;
        }
        const Id t = edit::addTrack (p, TrackKind::Track, name);
        auto* track = p.track (t);
        std::string line = name + ":";
        auto addDevice = [&] (const Choice& c, DeviceRole role) -> Id
        {
            const Id d = edit::addDevice (p, t, c.ref, role);
            line += (line.back() == ':' ? " " : ", ") + c.name;
            return d;
        };

        if (part == Part::Loop)
        {
            // The loop, stretched to the tempo, over the whole song.
            Clip c;
            c.kind = Clip::Kind::Audio;
            c.name = name;
            c.start = 0;
            c.length = length;
            c.audio.file = loop->path;
            c.audio.sourceLength = loop->seconds;
            c.audio.fileSampleRate = loop->sampleRate;
            c.audio.fileChannels = loop->channels;
            c.audio.sourceBpm = loop->bpm;
            c.audio.warp = WarpMode::Spectral;
            c.loopLength = std::max<Tick> (PPQ, (Tick) std::llround (loop->seconds * loop->bpm / 60.0 * PPQ));
            c.audio.gainDb = -6;
            edit::addClip (p, t, c);
            loopsLeft.pop_back();
            line += " a loop from your sounds";
        }
        else
        {
            // An instrument: drums want a kit, or the Sampler with a drum hit; the rest any other.
            const Choice* inst = nullptr;
            std::vector<Choice> usable;   // kept while `inst` may point into it
            if (part == Part::Drums)
            {
                inst = pick (drumKits, rng);
                if (! inst)
                    for (auto& c : tonal) if (c.wantsSample && ! drumHits.empty()) { inst = &c; break; }
            }
            if (! inst)
            {
                // A Sampler only when there's a sound for it to play.
                for (auto& c : tonal) if (! c.wantsSample || ! tonalHits.empty() || (part == Part::Drums && ! drumHits.empty())) usable.push_back (c);
                inst = pick (usable, rng);
            }
            // A MIDI effect, now and then, before the instrument.
            if (midiFxLeft > 0 && part != Part::Drums && chance (0.25))
                if (auto* m = pick (cat.midiEffects, rng)) { addDevice (*m, DeviceRole::MidiFx); --midiFxLeft; }
            if (inst)
            {
                const Id d = addDevice (*inst, DeviceRole::Instrument);
                if (inst->wantsSample)
                {
                    const auto& from = part == Part::Drums && ! drumHits.empty() ? drumHits : tonalHits;
                    if (auto* s = pick (from, rng)) { r.samplerSamples.push_back ({ d, s->path }); line += " (" + s->name + ")"; }
                }
            }
            // The part's notes.
            std::vector<Note> notes;
            switch (part)
            {
                case Part::Drums:   notes = groove.notes; break;
                case Part::Bass:    notes = harmony.bass; break;
                case Part::Chords:  notes = harmony.chords; break;
                case Part::Pad:     for (auto n : harmony.chords) { n.pitch = std::min (127, n.pitch + 12); n.velocity = std::max (1, n.velocity - 25); notes.push_back (n); } break;
                case Part::Lead:    notes = lead; break;
                case Part::Counter: for (auto n : counter) { n.pitch = std::min (127, n.pitch + 12); notes.push_back (n); } break;
                default: break;
            }
            Clip c;
            c.kind = Clip::Kind::Midi;
            c.name = name;
            c.start = 0;
            c.length = length;
            for (auto n : notes) { n.id = 0; if (n.start < length) c.midi.notes.push_back (n); }
            edit::sortNotes (c.midi);
            edit::addClip (p, t, c);
        }

        // Effects: none, one or two, while there are any left to give.
        const int effects = std::min ({ between (0, 2), effectsLeft, (int) cat.effects.size() });
        std::vector<std::string> used;
        for (int i = 0; i < effects; ++i)
        {
            const Choice* e = pick (cat.effects, rng);
            if (! e || std::find (used.begin(), used.end(), e->name) != used.end()) continue;
            used.push_back (e->name);
            addDevice (*e, DeviceRole::AudioFx);
            --effectsLeft;
        }

        // A rough mix: the drums and bass forward, the rest under, spread a little.
        track = p.track (t);
        switch (part)
        {
            case Part::Drums: track->volumeDb = -4; break;
            case Part::Bass: track->volumeDb = -6; break;
            case Part::Lead: track->volumeDb = -8; track->pan = std::uniform_real_distribution<double> (-0.15, 0.15) (rng); break;
            default: track->volumeDb = -12; track->pan = std::uniform_real_distribution<double> (-0.45, 0.45) (rng); break;
        }
        r.parts.push_back (line);
    }

    r.summary = std::to_string (p.tracks.size()) + " tracks at " + std::to_string ((int) bpm) + " BPM in "
              + scaleview::roots[(size_t) p.scale.root].name + " " + scaleview::scales[(size_t) p.scale.scale].name;
    if (! harmony.names.empty()) r.summary += ", " + harmony.names;
    return r;
}

} // namespace kal::randomsong
