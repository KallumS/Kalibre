#include "Check.h"
#include "AudioTools.h"

#include <cmath>
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
