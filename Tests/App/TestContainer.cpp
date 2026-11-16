// The Container device (decision 0055): plug-ins one after another, each on
// a band, the mid or the sides, with on, solo, volume and pan, and Match Loudness.
#include "Check.h"
#include "ContainerProcessor.h"
#include "PluginManager.h"

#include <cmath>

using namespace kal;
using namespace juce;

namespace
{
constexpr double rate = 48000;
constexpr int block = 512;

PluginRef builtIn (const String& id)
{
    PluginRef r;
    r.format = "Kalibre";
    r.identifier = id.toStdString();
    r.name = id.toStdString();
    return r;
}

// Runs `seconds` of the signal through the container, a block at a time.
AudioBuffer<float> run (ContainerProcessor& c, std::function<std::pair<float, float> (int)> signal, double seconds)
{
    const int total = (int) (seconds * rate);
    AudioBuffer<float> out (2, total), b (2, block);
    MidiBuffer m;
    for (int at = 0; at < total; at += block)
    {
        const int n = jmin (block, total - at);
        AudioBuffer<float> view (b.getArrayOfWritePointers(), 2, n);
        for (int i = 0; i < n; ++i)
        {
            const auto [l, r] = signal (at + i);
            view.setSample (0, i, l);
            view.setSample (1, i, r);
        }
        c.processBlock (view, m);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, at, view, ch, 0, n);
    }
    return out;
}

float sine (int i, double hz) { return (float) std::sin (2.0 * MathConstants<double>::pi * hz * i / rate); }

// How much of a frequency is in the last half second of a channel (amplitude).
float amount (const AudioBuffer<float>& b, int ch, double hz)
{
    const int n = (int) (rate * 0.5), from = b.getNumSamples() - n;
    double re = 0, im = 0;
    for (int i = 0; i < n; ++i)
    {
        const double ph = 2.0 * MathConstants<double>::pi * hz * (from + i) / rate;
        re += b.getSample (ch, from + i) * std::cos (ph);
        im += b.getSample (ch, from + i) * std::sin (ph);
    }
    return (float) (2.0 * std::sqrt (re * re + im * im) / n);
}

float peakOf (const AudioBuffer<float>& b, int ch, int from)
{
    float p = 0;
    for (int i = from; i < b.getNumSamples(); ++i) p = jmax (p, std::abs (b.getSample (ch, i)));
    return p;
}

void set (RangedAudioParameter* p, float value) { p->setValueNotifyingHost (p->convertTo0to1 (value)); }

struct Rig
{
    PluginManager plugins;
    ContainerProcessor c { plugins };
    Rig() { c.setPlayConfigDetails (2, 2, rate, block); c.prepareToPlay (rate, block); }
    void add (int i, const String& id) { String e; CHECK (c.loadPlugin (i, builtIn (id), e)); }
};
} // namespace

TEST ("container: plug-ins one after another, each with its volume; off and solo")
{
    Rig r;
    r.add (0, "test-gain:-6");
    r.add (1, "test-gain:-6");
    auto tone = [] (int i) { return std::pair<float, float> { 0.5f * sine (i, 440), 0.5f * sine (i, 440) }; };
    auto db = [] (float a) { return Decibels::gainToDecibels (a / 0.5f); };

    CHECK (std::abs (db (amount (run (r.c, tone, 1.0), 0, 440)) + 12.0f) < 0.2f);   // -6 and -6
    r.c.slot (1).on->setValueNotifyingHost (0.0f);
    CHECK (std::abs (db (amount (run (r.c, tone, 1.0), 0, 440)) + 6.0f) < 0.2f);    // the second off
    r.c.slot (1).on->setValueNotifyingHost (1.0f);
    r.c.slot (0).solo->setValueNotifyingHost (1.0f);
    CHECK (std::abs (db (amount (run (r.c, tone, 1.0), 0, 440)) + 6.0f) < 0.2f);    // only the first
    r.c.slot (0).solo->setValueNotifyingHost (0.0f);
    set (r.c.slot (1).gainDb, 6.0f);                                                     // the second's own volume back up
    CHECK (std::abs (db (amount (run (r.c, tone, 1.0), 0, 440)) + 6.0f) < 0.2f);
    // Panned left: the right side is silent.
    set (r.c.slot (1).pan, -1.0f);
    const auto panned = run (r.c, tone, 1.0);
    CHECK (amount (panned, 1, 440) < 0.001f);
    CHECK (amount (panned, 0, 440) > 0.2f);
}

TEST ("container: a plug-in on a band changes only that band; what it doesn't touch is as loud as it was, and in time")
{
    Rig r;
    r.add (0, "test-gain:-100");   // all but silences what it hears
    set (r.c.slot (0).low, 500.0f);
    set (r.c.slot (0).high, 8000.0f);
    auto two = [] (int i) { const float v = 0.4f * sine (i, 100) + 0.4f * sine (i, 2000); return std::pair<float, float> { v, v }; };
    const auto out = run (r.c, two, 1.5);
    CHECK (amount (out, 0, 100) > 0.38f);    // below the band: kept
    CHECK (amount (out, 0, 2000) < 0.02f);   // in the band: gone

    // A plug-in that changes nothing, on a band, after a delay: every frequency
    // comes out as loud as it went in, even at the band's edges, where a
    // part held back the wrong amount would cancel.
    Rig t;
    t.add (0, "test-latency:100");
    set (t.c.slot (0).low, 300.0f);
    set (t.c.slot (0).high, 3000.0f);
    CHECK_EQ (t.c.getLatencySamples(), 100);
    for (double f : { 80.0, 300.0, 1000.0, 3000.0, 9000.0 })
    {
        const auto through = run (t.c, [f] (int i) { const float v = 0.5f * sine (i, f); return std::pair<float, float> { v, v }; }, 1.0);
        CHECK (std::abs (Decibels::gainToDecibels (amount (through, 0, f) / 0.5f)) < 0.1f);
    }
    Random random (7);
    std::vector<float> noise ((size_t) rate);
    for (auto& v : noise) v = random.nextFloat() - 0.5f;
    float worst = 0;
    // Switched off it still holds the sound back as long, so the track stays lined up.
    t.c.slot (0).on->setValueNotifyingHost (0.0f);
    const auto off = run (t.c, [&noise] (int i) { return std::pair<float, float> { noise[(size_t) i], noise[(size_t) i] }; }, 1.0);
    worst = 0;
    for (int i = 100; i < (int) rate; ++i) worst = jmax (worst, std::abs (off.getSample (0, i) - noise[(size_t) (i - 100)]));
    CHECK (worst < 1.0e-6f);
}

TEST ("container: on the mid it changes only the middle; on the sides only the sides")
{
    // Left only: half of it is mid (both sides alike), half side (where they differ).
    auto leftOnly = [] (int i) { return std::pair<float, float> { 0.5f * sine (i, 440), 0.0f }; };
    {
        Rig r;
        r.add (0, "test-gain:-100");
        r.c.slot (0).width->setValueNotifyingHost (r.c.slot (0).width->convertTo0to1 (2.0f));   // Side
        const auto out = run (r.c, leftOnly, 1.0);
        // The sides gone, the middle left: a quarter on each side, alike.
        CHECK (std::abs (amount (out, 0, 440) - 0.25f) < 0.01f);
        CHECK (std::abs (amount (out, 1, 440) - 0.25f) < 0.01f);
    }
    {
        Rig r;
        r.add (0, "test-gain:-100");
        r.c.slot (0).width->setValueNotifyingHost (r.c.slot (0).width->convertTo0to1 (1.0f));   // Mid
        const auto out = run (r.c, leftOnly, 1.0);
        // The middle gone, the sides left: a quarter each, opposite.
        CHECK (std::abs (amount (out, 0, 440) - 0.25f) < 0.01f);
        CHECK (std::abs (amount (out, 1, 440) - 0.25f) < 0.01f);
        CHECK (out.getSample (0, 12000) * out.getSample (1, 12000) <= 0.0f);
    }
}

TEST ("container: Match Loudness brings what goes out to the short-term loudness of what came in")
{
    Rig r;
    r.add (0, "test-gain:-10");
    Random random (3);
    auto noise = [&random] (int) { const float v = 0.2f * (random.nextFloat() - 0.5f); return std::pair<float, float> { v, v }; };
    const auto plain = run (r.c, noise, 2.0);
    const float quiet = peakOf (plain, 0, 48000);
    r.c.matchLoudness->setValueNotifyingHost (1.0f);
    const auto matched = run (r.c, noise, 8.0);
    CHECK (std::abs (r.c.appliedGainDb.load() - 10.0f) < 1.0f);
    CHECK (peakOf (matched, 0, (int) (rate * 7)) > quiet * 2.5f);
}

TEST ("container: its plug-ins and settings come back from its state")
{
    Rig r;
    r.add (0, "test-gain:-6");
    r.add (1, "test-latency:50");
    set (r.c.slot (1).low, 250.0f);
    set (r.c.slot (0).pan, 0.5f);
    r.c.slot (1).width->setValueNotifyingHost (r.c.slot (1).width->convertTo0to1 (1.0f));
    r.c.matchLoudness->setValueNotifyingHost (1.0f);
    MemoryBlock state;
    r.c.getStateInformation (state);

    Rig back;
    back.c.setStateInformation (state.getData(), (int) state.getSize());
    CHECK_EQ (back.c.firstEmpty(), 2);
    CHECK_EQ (String (back.c.slot (1).ref.identifier), String ("test-latency:50"));
    CHECK (std::abs (back.c.slot (1).low->get() - 250.0f) < 1.0f);
    CHECK (std::abs (back.c.slot (0).pan->get() - 0.5f) < 0.01f);
    CHECK_EQ (back.c.slot (1).width->getIndex(), 1);
    CHECK (back.c.matchLoudness->get());
    CHECK_EQ (back.c.getLatencySamples(), 50);

    // Removing the first moves the second up, with its settings.
    back.c.clearSlot (0);
    CHECK_EQ (back.c.firstEmpty(), 1);
    CHECK_EQ (String (back.c.slot (0).ref.identifier), String ("test-latency:50"));
    CHECK (std::abs (back.c.slot (0).low->get() - 250.0f) < 1.0f);
    CHECK_EQ (back.c.slot (0).width->getIndex(), 1);
}
