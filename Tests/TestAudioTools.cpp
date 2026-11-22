#include "Check.h"
#include "AudioTools.h"
#include "Edit.h"
#include "Playback.h"

#include <cmath>
#include <functional>
#include <vector>

using namespace kal;
using namespace kal::audiotools;

static constexpr double tau = 6.283185307179586;

TEST ("audio tools: normalise finds the gain to the target")
{
    std::vector<float> a { 0.0f, 0.25f, -0.5f, 0.1f };
    const float* ch[] { a.data() };
    CHECK_EQ (peak (ch, 1, 0, 4), 0.5f);
    CHECK (std::abs (normaliseGainDb (0.5f, 0.0) - 6.0206) < 1e-3);
}

TEST ("audio tools: silence between two notes is found; short gaps aren't")
{
    const double rate = 1000;
    std::vector<float> a (3000, 0.0f);
    for (int i = 100; i < 600; ++i) a[(size_t) i] = 0.5f;       // a note
    for (int i = 650; i < 900; ++i) a[(size_t) i] = 0.5f;       // 50 ms later: the same sound
    for (int i = 2000; i < 2500; ++i) a[(size_t) i] = 0.5f;     // a second later: a new one
    const float* ch[] { a.data() };
    const auto s = sounding (ch, 1, 3000, rate, -40.0, 0.15, 0.0);
    CHECK_EQ (s.size(), (size_t) 2);
    CHECK_EQ (s[0].start, 100);
    CHECK_EQ (s[0].end, 900);
    CHECK_EQ (s[1].start, 2000);
    CHECK_EQ (s[1].end, 2500);
}

TEST ("audio tools: hits are found within a few milliseconds")
{
    const double rate = 48000;
    std::vector<float> a ((size_t) rate * 2, 0.0f);
    const std::vector<int> at { 4800, 30000, 60000 };
    for (int start : at)
        for (int i = 0; i < 4000; ++i)
            a[(size_t) (start + i)] = (float) (0.8 * std::exp (-i / 800.0) * std::sin (tau * 200 * i / rate));
    const float* ch[] { a.data() };
    const auto found = transients (ch, 1, (int) a.size(), rate, 0.5);
    CHECK_EQ (found.size(), at.size());
    for (size_t k = 0; k < found.size() && k < at.size(); ++k)
        CHECK (std::abs (found[k] - at[k]) < (int) (rate * 0.006));
}

TEST ("audio tools: audio quantise moves a late hit onto the beat, pinning the ends")
{
    TempoMap tempo;   // 120 BPM: a quarter note is half a second
    AudioData a;
    a.sourceStart = 0;
    a.sourceBpm = 120;
    // A hit 0.55 s in: 10% of a beat late.
    const auto m = quantiseMarkers (a, { 0.55 }, tempo, 0, PPQ * 4, PPQ, 1.0);
    CHECK_EQ (m.size(), (size_t) 3);
    CHECK_EQ (m[0].tick, (Tick) 0);
    CHECK_EQ (m[1].tick, PPQ);
    CHECK (std::abs (m[1].sourceSeconds - 0.55) < 1e-9);
    CHECK_EQ (m[2].tick, PPQ * 4);
    CHECK (std::abs (m[2].sourceSeconds - 2.0) < 1e-9);
    // Half strength moves it half way.
    const auto h = quantiseMarkers (a, { 0.55 }, tempo, 0, PPQ * 4, PPQ, 0.5);
    CHECK_EQ (h[1].tick, PPQ + PPQ / 20);
}

TEST ("audio tools: tick and source seconds are each other's inverse")
{
    TempoMap tempo;
    AudioData a;
    a.sourceStart = 1.0;
    a.markers = { { 1.0, 0 }, { 2.0, PPQ * 3 }, { 3.0, PPQ * 4 } };
    for (Tick t : { (Tick) 0, PPQ, PPQ * 3, PPQ * 7 / 2 })
        CHECK_EQ (tickForSource (a, sourceSecondsAt (a, t), tempo, 0), t);
    AudioData own;   // no tempo known: at its own speed through the song's tempo
    own.sourceStart = 0.5;
    CHECK_EQ (tickForSource (own, 1.0, tempo, 0), PPQ);
}

TEST ("audio tools: sample edits")
{
    std::vector<float> a { 1, 2, 3, 4, 5 };
    float* ch[] { a.data() };
    reverse (ch, 1, 1, 4);
    CHECK_EQ (a, (std::vector<float> { 1, 4, 3, 2, 5 }));
    silence (ch, 1, 0, 2);
    CHECK_EQ (a, (std::vector<float> { 0, 0, 3, 2, 5 }));
    invert (ch, 1, 4, 5);
    CHECK_EQ (a[4], -5.0f);
    std::vector<float> dc (100, 0.25f);
    float* d[] { dc.data() };
    removeDc (d, 1, 0, 100);
    CHECK (std::abs (dc[50]) < 1e-6f);
    std::vector<float> f (10, 1.0f);
    float* fc[] { f.data() };
    fade (fc, 1, 0, 10, true);
    CHECK_EQ (f[0], 0.0f);
    CHECK (f[9] > 0.95f);
}

TEST ("audio tools: a sine's spectrum peaks at its bin; YIN hears its pitch")
{
    const double rate = 48000;
    const int n = 4096;
    std::vector<float> s ((size_t) n);
    const double hz = rate / n * 100;   // exactly bin 100
    for (int i = 0; i < n; ++i) s[(size_t) i] = (float) std::sin (tau * hz * i / rate);
    const auto spec = spectrumDb (s.data(), n);
    size_t top = 0;
    for (size_t k = 0; k < spec.size(); ++k) if (spec[k] > spec[top]) top = k;
    CHECK_EQ (top, (size_t) 100);
    CHECK (std::abs (spec[100]) < 0.5f);

    std::vector<float> a4 ((size_t) n);
    for (int i = 0; i < n; ++i) a4[(size_t) i] = (float) (0.5 * std::sin (tau * 440.0 * i / rate));
    const double found = pitchOf (a4.data(), n, rate);
    CHECK (std::abs (found - 440.0) < 1.0);
    int note = 0;
    double cents = 0;
    nearestNote (found, note, cents);
    CHECK_EQ (note, 69);
    CHECK (std::abs (cents) < 5);
    nearestNote (452.0, note, cents);
    CHECK_EQ (note, 69);
    CHECK (cents > 40 && cents < 50);
}

#include "Edit.h"

TEST ("audio tools: strip silence leaves one clip a sound, each where it was")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Vox");
    Clip c;
    c.kind = Clip::Kind::Audio;
    c.start = PPQ * 4;            // bar 2, at 120 BPM: 2 s in
    c.length = PPQ * 8;           // 4 s of a file
    c.audio.sourceStart = 0;
    c.audio.sourceLength = 4;
    const Id id = edit::addClip (p, t, c);
    const auto ids = edit::keepSourceSpans (p, id, { { 0.5, 1.0 }, { 3.0, 5.0 } });
    CHECK_EQ (ids.size(), (size_t) 2);
    const auto& clips = p.track (t)->clips();
    CHECK_EQ (clips.size(), (size_t) 2);
    CHECK_EQ (clips[0].start, PPQ * 5);       // 0.5 s = one beat in
    CHECK_EQ (clips[0].length, PPQ);
    CHECK_EQ (clips[0].offset, PPQ);
    CHECK_EQ (clips[1].start, PPQ * 10);
    CHECK_EQ (clips[1].length, PPQ * 2);      // cut at the clip's own end
}

TEST ("audio tools: the nearest zero crossing, across both channels, within reach")
{
    // A 100 Hz sine at 48 kHz crosses zero every 240 samples: at 0, 240, 480...
    const int n = 4800;
    std::vector<float> l (n), r (n);
    for (int i = 0; i < n; ++i) l[(size_t) i] = r[(size_t) i] = (float) std::sin (2.0 * 3.14159265358979 * 100.0 * (i + 0.5) / 48000.0);
    const float* ch[] { l.data(), r.data() };
    // Asked for 700: the crossing at 720 is the nearest (between 719 and 720).
    const int z = audiotools::nearestZeroCrossing (ch, 2, n, 700, 480);
    CHECK (z == 720 || z == 719);
    CHECK (std::abs (l[(size_t) z]) < 0.01f);
    // Out of reach: where it was asked.
    CHECK_EQ (audiotools::nearestZeroCrossing (ch, 2, n, 600, 10), 600);
    // Silence counts as zero.
    std::vector<float> quiet (100, 0.0f);
    const float* q[] { quiet.data() };
    CHECK_EQ (audiotools::nearestZeroCrossing (q, 1, 100, 50, 10), 50);
}

namespace
{
// A drum loop: a low thump on every beat, a click on every off-beat, `bars` bars of 4/4.
std::vector<float> drumLoop (double bpm, double seconds, double rate)
{
    std::vector<float> x ((size_t) (seconds * rate), 0.0f);
    const double beat = 60.0 / bpm;
    for (int b = 0; b * beat < seconds; ++b)
        for (int half = 0; half < 2; ++half)
        {
            const auto start = (size_t) ((b + half * 0.5) * beat * rate);
            for (size_t i = 0; i < (size_t) (0.12 * rate) && start + i < x.size(); ++i)
            {
                const double t = (double) i / rate;
                x[start + i] += half == 0 ? (float) (0.8 * std::sin (2 * M_PI * 60 * t) * std::exp (-t * 30))
                                          : (float) (0.3 * std::sin (2 * M_PI * 3000 * t + std::sin (i * 1.7)) * std::exp (-t * 80));
            }
        }
    return x;
}
} // namespace

TEST ("audio tools: a loop's tempo is heard, and made exact by its length (0095)")
{
    const double rate = 44100;
    for (auto [bpm, bars] : std::vector<std::pair<double, int>> { { 120, 2 }, { 93, 2 }, { 140, 2 }, { 75, 2 }, { 100, 1 },
                                                                    { 128, 4 }, { 170, 2 }, { 85, 1 }, { 110, 8 } })
    {
        const double seconds = bars * 4 * 60.0 / bpm;
        auto x = drumLoop (bpm, seconds, rate);
        const auto g = audiotools::tempoOf (x.data(), (int) x.size(), rate);
        CHECK (g.loop);
        CHECK (std::abs (g.bpm - bpm) < 0.01);
        if (std::abs (g.bpm - bpm) >= 0.01) std::printf ("    %.1f (%d bars) heard as %.2f\n", bpm, bars, g.bpm);
    }
    // Not cut to its bars: heard, roughly, and not called a loop.
    auto y = drumLoop (128, 5.3, rate);
    const auto g = audiotools::tempoOf (y.data(), (int) y.size(), rate);
    CHECK (! g.loop);
    CHECK (std::abs (g.bpm - 128) < 2.0);
    // Noise has no beat; a one-shot is too short to have one.
    std::vector<float> noise ((size_t) (rate * 4));
    unsigned seed = 1;
    for (auto& v : noise) { seed = seed * 1664525u + 1013904223u; v = (float) ((seed >> 8) / 16777216.0 - 0.5); }
    CHECK_EQ (audiotools::tempoOf (noise.data(), (int) noise.size(), rate).bpm, 0.0);
    CHECK_EQ (audiotools::tempoOf (y.data(), (int) (rate * 0.5), rate).bpm, 0.0);
}

TEST ("audio tools: a sample's note is heard; drums and noise have none (0095)")
{
    const double rate = 48000;
    std::vector<float> a3 ((size_t) rate * 2);
    for (size_t i = 0; i < a3.size(); ++i)
    {
        const double t = (double) i / rate;
        a3[i] = (float) (0.5 * std::sin (2 * M_PI * 220 * t) + 0.2 * std::sin (2 * M_PI * 440 * t)) * (float) std::exp (-t);
    }
    double cents = 99;
    CHECK_EQ (audiotools::noteOf (a3.data(), (int) a3.size(), rate, &cents), 57);
    CHECK (std::abs (cents) < 5);
    // A quarter-tone sharp of C4 says so.
    std::vector<float> c4 ((size_t) rate);
    const double hz = 261.6256 * std::pow (2.0, 30.0 / 1200.0);
    for (size_t i = 0; i < c4.size(); ++i) c4[i] = (float) (0.5 * std::sin (2 * M_PI * hz * (double) i / rate));
    CHECK_EQ (audiotools::noteOf (c4.data(), (int) c4.size(), rate, &cents), 60);
    CHECK (std::abs (cents - 30) < 5);
    std::vector<float> noise ((size_t) rate);
    unsigned seed = 7;
    for (auto& v : noise) { seed = seed * 1664525u + 1013904223u; v = (float) ((seed >> 8) / 16777216.0 - 0.5); }
    CHECK_EQ (audiotools::noteOf (noise.data(), (int) noise.size(), rate), -1);
    std::vector<float> silence ((size_t) rate, 0.0f);
    CHECK_EQ (audiotools::noteOf (silence.data(), (int) silence.size(), rate), -1);
}

TEST ("audio tools: Split at Transients cuts a clip where each hit is heard, side by side (0095)")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Drums");
    Clip c;
    c.kind = Clip::Kind::Audio;
    c.start = PPQ * 4;            // at 120 BPM: 2 s in
    c.length = PPQ * 8;           // 4 s of a file
    c.audio.sourceLength = 4;
    const Id id = edit::addClip (p, t, c);
    // Hits at 0 (the clip's own start: no cut), 1 s, 2.5 s, and past its end.
    const auto ids = edit::splitAtSource (p, id, { 0.0, 1.0, 2.5, 6.0 });
    CHECK_EQ (ids.size(), (size_t) 3);
    CHECK_EQ (ids.front(), id);
    const auto& clips = p.track (t)->clips();
    CHECK_EQ (clips.size(), (size_t) 3);
    CHECK_EQ (clips[0].start, PPQ * 4);
    CHECK_EQ (clips[0].length, PPQ * 2);
    CHECK_EQ (clips[1].start, PPQ * 6);
    CHECK_EQ (clips[1].offset, PPQ * 2);
    CHECK_EQ (clips[1].length, PPQ * 3);
    CHECK_EQ (clips[2].start, PPQ * 9);
    CHECK_EQ (clips[2].end(), PPQ * 12);
    for (size_t i = 0; i < 3; ++i) CHECK_EQ (clips[i].id, ids[i]);
}

TEST ("edit: Reverse and Invert, for MIDI and audio clips (0095)")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Keys");
    const Id m = edit::addMidiClip (p, t, 0, PPQ * 4);
    edit::addNote (p, m, { 0, 0, PPQ, 60, 100 });          // C, the first beat
    edit::addNote (p, m, { 0, PPQ * 2, PPQ * 2, 67, 90 });  // G, the second half
    p.clip (m)->midi.events.push_back ({ PPQ, EventType::Controller, 0, 1, 64 });
    edit::reverseClip (p, m);
    const auto& notes = p.clip (m)->midi.notes;
    CHECK_EQ (notes[0].pitch, 67);
    CHECK_EQ (notes[0].start, Tick (0));
    CHECK_EQ (notes[1].pitch, 60);
    CHECK_EQ (notes[1].start, PPQ * 3);
    CHECK_EQ (p.clip (m)->midi.events[0].tick, PPQ * 3 - 1);
    edit::invertClip (p, m);
    CHECK_EQ (p.clip (m)->midi.notes[0].pitch, 60);   // highest and lowest swap
    CHECK_EQ (p.clip (m)->midi.notes[1].pitch, 67);
    // Twice is back where it was.
    edit::reverseClip (p, m);
    CHECK_EQ (p.clip (m)->midi.notes[0].start, Tick (0));
    CHECK_EQ (p.clip (m)->midi.notes[0].pitch, 67);

    Clip a;
    a.kind = Clip::Kind::Audio;
    a.length = PPQ;
    a.audio.sourceLength = 0.5;
    const Id id = edit::addClip (p, t, a);
    edit::invertClip (p, id);
    edit::reverseClip (p, id);
    CHECK (p.clip (id)->audio.inverted);
    CHECK (p.clip (id)->audio.reversed);
    // Played upside down: its region's gain is negative.
    const auto compiled = playback::compile (p, *p.track (t));
    CHECK_EQ (compiled.audio.size(), (size_t) 1);
    CHECK (compiled.audio[0].gain < 0);
}

namespace
{
// A hit: `delayMs` of silence, a rise over `riseMs`, then a decay of `decayMs` (to 1/e) of `tone`.
std::vector<float> hit (double rate, double seconds, double delayMs, double riseMs, double decayMs, std::function<float (int)> tone)
{
    std::vector<float> x ((size_t) (rate * seconds), 0.0f);
    const int from = (int) (delayMs * rate / 1000);
    for (size_t i = (size_t) from; i < x.size(); ++i)
    {
        const double t = (double) (i - (size_t) from) / rate * 1000.0;
        const double env = (riseMs > 0 ? std::min (1.0, t / riseMs) : 1.0) * std::exp (-std::max (0.0, t - riseMs) / decayMs);
        x[i] = (float) (0.8 * env * tone ((int) i));
    }
    return x;
}
} // namespace

TEST ("hit shape: when a sound starts, how fast it rises, how far it dies away, and how bright it is (0106)")
{
    const double rate = 48000;
    // A kick: low, at once, dying in a quarter of a second.
    const auto kick = hit (rate, 0.5, 0, 0, 60, [=] (int i) { return (float) std::sin (tau * 55.0 * i / rate); });
    const auto k = hitShape (kick.data(), (int) kick.size(), rate);
    CHECK (k.onsetMs >= 0 && k.onsetMs < 2);
    CHECK (k.attackMs >= 0 && k.attackMs < 10);
    CHECK (k.tailDb < -20);
    CHECK (k.zcr > 0 && k.zcr < 400);   // 55 Hz crosses zero 110 times a second

    // A hat: noise, after 20 ms of silence, short.
    unsigned seed = 7;
    auto noise = [&seed] (int) { seed = seed * 1664525u + 1013904223u; return (float) ((seed >> 8) / 8388608.0 - 1.0); };
    const auto hat = hit (rate, 0.3, 20, 0, 25, noise);
    const auto h = hitShape (hat.data(), (int) hat.size(), rate);
    CHECK (std::abs (h.onsetMs - 20) < 2);
    CHECK (h.zcr > 9000);

    // A pad: rising over half a second, held to the end.
    const auto pad = hit (rate, 2.0, 0, 500, 100000, [=] (int i) { return (float) std::sin (tau * 220.0 * i / rate); });
    const auto p = hitShape (pad.data(), (int) pad.size(), rate);
    CHECK (p.attackMs > 300);
    CHECK (p.tailDb > -6);

    // Silence: nothing heard.
    std::vector<float> none (4800, 0.0f);
    CHECK (hitShape (none.data(), (int) none.size(), rate).onsetMs < 0);
}

TEST ("audio tools: a held note has no tempo, however long: a pad, a sub bass (the UAT's read 'about 120' and 'about 112')")
{
    const double rate = 48000;
    auto held = [rate] (double f, double seconds, int harmonics, double attack, double release)
    {
        std::vector<float> x ((size_t) (rate * seconds));
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = (double) i / rate;
            const double env = std::min (1.0, t / attack) * std::min (1.0, (seconds - t) / release);
            double s = 0;
            for (int k = 1; k <= harmonics; ++k) s += std::sin (2 * 3.14159265358979 * f * k * t) / k;
            x[i] = (float) (0.35 * env * s);
        }
        return x;
    };
    for (auto& [what, x] : std::vector<std::pair<const char*, std::vector<float>>> {
             { "pad", held (220.0, 3.0, 5, 0.3, 0.8) }, { "sub", held (65.41, 2.0, 2, 0.01, 0.3) }, { "long", held (110.0, 8.0, 6, 0.05, 1.0) } })
    {
        const auto g = audiotools::tempoOf (x.data(), (int) x.size(), rate);
        if (g.bpm > 0) std::printf ("    %s heard at %.1f BPM\n", what, g.bpm);
        CHECK_EQ (g.bpm, 0.0);
    }
}
