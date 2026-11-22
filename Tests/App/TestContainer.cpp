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

TEST ("container: a plug-in at 0 dB leaves the sound exactly as it was; its pan is the mixer's, constant power (0081)")
{
    Rig r;
    r.add (0, "test-gain:0");
    r.add (1, "test-gain:0");
    auto noise = [] (int i) { const float v = (float) ((i * 7919) % 2001 - 1000) / 1024.0f; return std::pair<float, float> { v, -v * 0.5f }; };
    const auto out = run (r.c, noise, 0.5);
    float worst = 0;
    for (int i = 0; i < out.getNumSamples(); ++i)
    {
        const auto [l, rr] = noise (i);
        worst = jmax (worst, std::abs (out.getSample (0, i) - l), std::abs (out.getSample (1, i) - rr));
    }
    CHECK_EQ (worst, 0.0f);
    // Fully left: +3 dB on the left, nothing on the right.
    set (r.c.slot (1).pan, -1.0f);
    auto tone = [] (int i) { return std::pair<float, float> { 0.5f * sine (i, 440), 0.5f * sine (i, 440) }; };
    const auto panned = run (r.c, tone, 1.0);
    CHECK (std::abs (Decibels::gainToDecibels (amount (panned, 0, 440) / 0.5f) - 3.0103f) < 0.05f);
    CHECK (amount (panned, 1, 440) < 1.0e-6f);
}

TEST ("container: on Mono the plug-in hears both sides as one and sends one back; a song saved before keeps its Stereo, Mid and Side (0118)")
{
    auto leftOnly = [] (int i) { return std::pair<float, float> { 0.5f * sine (i, 440), 0.0f }; };
    {
        // A plug-in that changes nothing, on Mono: the left alone comes out as a quarter on each side, alike.
        Rig r;
        r.add (0, "test-gain:0");
        r.c.slot (0).mono->setValueNotifyingHost (1.0f);
        const auto out = run (r.c, leftOnly, 1.0);
        CHECK (std::abs (amount (out, 0, 440) - 0.25f) < 0.01f);
        CHECK (std::abs (amount (out, 1, 440) - 0.25f) < 0.01f);
        CHECK (out.getSample (0, 12000) * out.getSample (1, 12000) >= 0.0f);
    }
    {
        // A plug-in that silences, on Mono: nothing passes by (Mid would leave the sides).
        Rig r;
        r.add (0, "test-gain:-100");
        r.c.slot (0).mono->setValueNotifyingHost (1.0f);
        const auto out = run (r.c, leftOnly, 1.0);
        CHECK (amount (out, 0, 440) < 0.001f);
        CHECK (amount (out, 1, 440) < 0.001f);
    }
    {
        // Every parameter that was there before Mono is where it was: Mono's come after them all.
        Rig r;
        const auto& ps = r.c.getParameters();
        CHECK_EQ (ps.size(), 1 + ContainerProcessor::slotCount * 8);
        for (int i = 0; i < ContainerProcessor::slotCount; ++i)
        {
            CHECK (ps[1 + i * 7 + 2] == r.c.slot (i).width);
            CHECK (ps[1 + ContainerProcessor::slotCount * 7 + i] == r.c.slot (i).mono);
        }
        // Side saved as before (its fraction, 1) is still Side.
        r.c.slot (0).width->setValueNotifyingHost (1.0f);
        CHECK (r.c.slot (0).width->getIndex() == 2);
        CHECK (! r.c.slot (0).mono->get());
    }
}

#include "BuiltinInstruments.h"
#include "Editors.h"

TEST ("container: it takes the size of the plug-in shown, when one is added and when another is chosen by its name, so the rack fits it whole (0118)")
{
    Rig r;
    editors::Owned editor (editors::open (r.c));
    CHECK (editor != nullptr);
    if (! editor) return;
    String e;
    CHECK (r.c.loadPlugin (0, synthRef(), e));
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    auto innerOf = [&]() -> AudioProcessorEditor*
    {
        for (auto* ch : editor->getChildren()) if (auto* ed = dynamic_cast<AudioProcessorEditor*> (ch)) return ed;
        return nullptr;
    };
    auto* synthWindow = innerOf();
    CHECK (synthWindow != nullptr);
    if (! synthWindow) return;
    // The whole of it inside the container: nothing cut off.
    auto fits = [&] (AudioProcessorEditor* in) { return editor->getLocalBounds().contains (in->getBounds()); };
    CHECK (fits (synthWindow));
    CHECK (editor->getWidth() >= synthWindow->getWidth() + 620);
    // Another added: it's shown, and the container takes its size.
    CHECK (r.c.loadPlugin (1, samplerRef(), e));
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    // Chosen by its name: shown whole.
    Button* samplerName = nullptr;
    std::function<void (Component&)> walk = [&] (Component& c)
    {
        if (auto* b = dynamic_cast<Button*> (&c); b && b->getButtonText() == "Kalibre Sampler") samplerName = b;
        for (auto* ch : c.getChildren()) walk (*ch);
    };
    walk (*editor);
    CHECK (samplerName != nullptr);
    if (! samplerName) return;
    samplerName->triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    auto* samplerWindow = innerOf();
    CHECK (samplerWindow != nullptr && samplerWindow != synthWindow);
    if (! samplerWindow) return;
    CHECK (fits (samplerWindow));
    // The plug-in grows its own window: the container grows with it.
    samplerWindow->setSize (samplerWindow->getWidth() + 200, samplerWindow->getHeight() + 150);
    CHECK (fits (samplerWindow));
}

// The Multiband device (decision 0126).
#include "MultibandProcessor.h"

namespace
{
struct BandRig
{
    PluginManager plugins;
    MultibandProcessor m { plugins };
    BandRig() { m.setPlayConfigDetails (2, 2, rate, block); m.prepareToPlay (rate, block); }
};

AudioBuffer<float> runBands (MultibandProcessor& m, std::function<std::pair<float, float> (int)> signal, double seconds)
{
    const int total = (int) (seconds * rate);
    AudioBuffer<float> out (2, total), b (2, block);
    MidiBuffer midi;
    for (int at = 0; at < total; at += block)
    {
        const int n = jmin (block, total - at);
        AudioBuffer<float> view (b.getArrayOfWritePointers(), 2, n);
        for (int i = 0; i < n; ++i) { const auto [l, r] = signal (at + i); view.setSample (0, i, l); view.setSample (1, i, r); }
        m.processBlock (view, midi);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, at, view, ch, 0, n);
    }
    return out;
}
} // namespace

TEST ("multiband: with nothing in it the sound is untouched, exactly; split with nothing in its bands, every frequency is as loud as it was (0126, 0081)")
{
    auto noise = [] (int i) { Random r (i); return std::pair<float, float> { r.nextFloat() - 0.5f, r.nextFloat() - 0.5f }; };
    {
        BandRig r;
        const auto out = runBands (r.m, noise, 0.5);
        double worst = 0;
        for (int i = 0; i < out.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto [l, rr] = noise (i);
                worst = jmax (worst, (double) std::abs (out.getSample (ch, i) - (ch == 0 ? l : rr)));
            }
        CHECK_EQ (worst, 0.0);   // cancels completely
    }
    // A band switched off (passing its sound by) splits the sound: across four bands every
    // frequency comes out as loud as it went in, near the crossovers too.
    for (double f : { 60.0, 150.0, 400.0, 1500.0, 3000.0, 6000.0, 12000.0 })
    {
        BandRig r;
        r.m.setBands (4);
        r.m.band (2).on->setValueNotifyingHost (0.0f);
        const auto out = runBands (r.m, [f] (int i) { return std::pair<float, float> { 0.5f * sine (i, f), 0.5f * sine (i, f) }; }, 1.0);
        CHECK (std::abs (Decibels::gainToDecibels (amount (out, 0, f) / 0.5f)) < 0.1f);
    }
}

TEST ("multiband: a plug-in on a band changes only that band; crossovers are dragged but never past one another (0126)")
{
    BandRig r;
    r.m.setBands (2);
    r.m.setCrossover (0, 1000.0f);
    String e;
    CHECK (r.m.loadPlugin (0, 0, builtIn ("test-gain:-100"), e));   // the low band silenced
    auto both = [] (int i) { return std::pair<float, float> { 0.4f * sine (i, 100) + 0.4f * sine (i, 6000), 0.4f * sine (i, 100) + 0.4f * sine (i, 6000) }; };
    const auto out = runBands (r.m, both, 1.0);
    CHECK (amount (out, 0, 100) < 0.4f * 0.05f);              // the low gone (more than 26 dB down)
    CHECK (std::abs (amount (out, 0, 6000) - 0.4f) < 0.02f);  // the high as it was

    r.m.setBands (4);
    r.m.setCrossover (0, 150.0f);
    r.m.setCrossover (1, 1500.0f);
    r.m.setCrossover (2, 6000.0f);
    r.m.setCrossover (0, 5000.0f);   // dragged past the next: stops short of it
    CHECK (r.m.crossoverHz (0) <= r.m.crossoverHz (1) / MultibandProcessor::minRatio + 0.5f);
    r.m.setCrossover (2, 100.0f);    // dragged below the one before: stops above it
    CHECK (r.m.crossoverHz (2) >= r.m.crossoverHz (1) * MultibandProcessor::minRatio - 0.5f);
    for (int k = 0; k + 1 < 3; ++k) CHECK (r.m.crossoverHz (k) < r.m.crossoverHz (k + 1));
}

TEST ("multiband: a band's plug-in delay holds the others back to meet it; its plug-ins come back from its state (0126)")
{
    BandRig r;
    r.m.setBands (3);
    String e;
    CHECK (r.m.loadPlugin (0, 0, builtIn ("test-latency:100"), e));
    CHECK_EQ (r.m.getLatencySamples(), 100);
    // A click comes out as it does with a plug-in of no delay in that band, 100 samples later
    // all of it: the other bands held back to meet the delayed one.
    auto click = [] (int i) { return std::pair<float, float> { i == 1000 ? 1.0f : 0.0f, i == 1000 ? 1.0f : 0.0f }; };
    const auto late = runBands (r.m, click, 0.1);
    BandRig plain;
    plain.m.setBands (3);
    CHECK (plain.m.loadPlugin (0, 0, builtIn ("test-gain:0"), e));
    const auto ontime = runBands (plain.m, click, 0.1);
    double worst = 0;
    for (int i = 0; i + 100 < late.getNumSamples(); ++i) worst = jmax (worst, (double) std::abs (late.getSample (0, i + 100) - ontime.getSample (0, i)));
    CHECK (worst < 1.0e-5);
    // Saved and opened again: the same bands, crossovers and plug-in.
    r.m.setCrossover (1, 3000.0f);
    MemoryBlock state;
    r.m.getStateInformation (state);
    BandRig back;
    back.m.setStateInformation (state.getData(), (int) state.getSize());
    CHECK_EQ (back.m.bands(), 3);
    CHECK (std::abs (back.m.crossoverHz (1) - r.m.crossoverHz (1)) < 1.0f);
    CHECK (back.m.band (0).slots[0].plugin != nullptr);
    CHECK_EQ (back.m.getLatencySamples(), 100);
}

// Stereo Split (decision 0127).
#include "StereoSplitProcessor.h"

namespace
{
struct StereoRig
{
    PluginManager plugins;
    StereoSplitProcessor s { plugins };
    StereoRig() { s.setPlayConfigDetails (2, 2, rate, block); s.prepareToPlay (rate, block); }
};

AudioBuffer<float> runStereo (AudioProcessor& m, std::function<std::pair<float, float> (int)> signal, double seconds)
{
    const int total = (int) (seconds * rate);
    AudioBuffer<float> out (2, total), b (2, block);
    MidiBuffer midi;
    for (int at = 0; at < total; at += block)
    {
        const int n = jmin (block, total - at);
        AudioBuffer<float> view (b.getArrayOfWritePointers(), 2, n);
        for (int i = 0; i < n; ++i) { const auto [l, r] = signal (at + i); view.setSample (0, i, l); view.setSample (1, i, r); }
        m.processBlock (view, midi);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, at, view, ch, 0, n);
    }
    return out;
}

// The most any sample of `out` differs from the signal `delay` samples before it.
double worstAgainst (const AudioBuffer<float>& out, std::function<std::pair<float, float> (int)> signal, int delay)
{
    double worst = 0;
    for (int i = delay; i < out.getNumSamples(); ++i)
    {
        const auto [l, r] = signal (i - delay);
        worst = jmax (worst, (double) std::abs (out.getSample (0, i) - l), (double) std::abs (out.getSample (1, i) - r));
    }
    return worst;
}
} // namespace

TEST ("stereo split: with nothing in it the sound is untouched, exactly (in Left, Middle, Right one window later); split with nothing done, the parts add back up to it (0127, 0081)")
{
    auto noise = [] (int i) { Random r (i); return std::pair<float, float> { r.nextFloat() - 0.5f, 0.3f * (r.nextFloat() - 0.5f) }; };
    {
        StereoRig r;
        CHECK_EQ (r.s.getLatencySamples(), StereoSplitProcessor::fftSize);
        CHECK_EQ (worstAgainst (runStereo (r.s, noise, 0.5), noise, StereoSplitProcessor::fftSize), 0.0);   // cancels completely
    }
    {
        StereoRig r;
        r.s.setMode (StereoSplitProcessor::Mode::middleSides);
        CHECK_EQ (r.s.getLatencySamples(), 0);
        CHECK_EQ (worstAgainst (runStereo (r.s, noise, 0.5), noise, 0), 0.0);
    }
    // A part switched off by nothing (a plug-in at 0 dB on it) splits the sound: the parts add back up to it.
    {
        StereoRig r;
        String e;
        CHECK (r.s.loadPlugin (StereoSplitProcessor::middle, 0, builtIn ("test-gain:0"), e));
        CHECK_EQ (r.s.getLatencySamples(), StereoSplitProcessor::fftSize);
        CHECK (worstAgainst (runStereo (r.s, noise, 0.5), noise, StereoSplitProcessor::fftSize) < 1.0e-5);
    }
    {
        StereoRig r;
        r.s.setMode (StereoSplitProcessor::Mode::middleSides);
        String e;
        CHECK (r.s.loadPlugin (StereoSplitProcessor::sides, 0, builtIn ("test-gain:0"), e));
        CHECK (worstAgainst (runStereo (r.s, noise, 0.5), noise, 0) < 1.0e-6);
    }
}

TEST ("stereo split: a plug-in on Left changes only what sits to the left; on Sides only what differs between them (0127)")
{
    // 300 Hz on the left alone, 2 kHz in the middle, 5 kHz on the right alone.
    auto three = [] (int i)
    {
        const float a = 0.3f * sine (i, 300), b = 0.3f * sine (i, 2000), c = 0.3f * sine (i, 5000);
        return std::pair<float, float> { a + b, b + c };
    };
    {
        StereoRig r;
        String e;
        CHECK (r.s.loadPlugin (StereoSplitProcessor::left, 0, builtIn ("test-gain:-100"), e));   // Left silenced
        const auto out = runStereo (r.s, three, 1.0);
        CHECK (amount (out, 0, 300) < 0.3f * 0.05f);                // the left gone
        CHECK (std::abs (amount (out, 0, 2000) - 0.3f) < 0.01f);    // the middle as it was, both sides
        CHECK (std::abs (amount (out, 1, 2000) - 0.3f) < 0.01f);
        CHECK (std::abs (amount (out, 1, 5000) - 0.3f) < 0.01f);    // the right as it was
    }
    {
        StereoRig r;
        String e;
        CHECK (r.s.loadPlugin (StereoSplitProcessor::middle, 0, builtIn ("test-gain:-100"), e));   // Middle silenced
        const auto out = runStereo (r.s, three, 1.0);
        CHECK (amount (out, 0, 2000) < 0.3f * 0.05f);
        CHECK (amount (out, 1, 2000) < 0.3f * 0.05f);
        CHECK (std::abs (amount (out, 0, 300) - 0.3f) < 0.01f);
        CHECK (std::abs (amount (out, 1, 5000) - 0.3f) < 0.01f);
    }
    {
        StereoRig r;
        r.s.setMode (StereoSplitProcessor::Mode::middleSides);
        String e;
        CHECK (r.s.loadPlugin (StereoSplitProcessor::sides, 0, builtIn ("test-gain:-100"), e));   // Sides silenced: mono
        const auto out = runStereo (r.s, three, 1.0);
        CHECK (std::abs (amount (out, 0, 300) - 0.15f) < 0.005f);   // the left's sound half on each side
        CHECK (std::abs (amount (out, 1, 300) - 0.15f) < 0.005f);
        CHECK (std::abs (amount (out, 0, 2000) - 0.3f) < 0.005f);   // the middle as it was
    }
}

TEST ("stereo split: its edges are dragged but never past one another; a part's plug-in delay is met; mode, edges and plug-ins come back from its state (0127)")
{
    StereoRig r;
    r.s.setEdge (0, 0.9f);   // dragged past the right edge: stops short of it
    CHECK (r.s.edge (0) <= r.s.edge (1) - StereoSplitProcessor::softness + 0.001f);
    r.s.setEdge (1, -1.0f);
    CHECK (r.s.edge (1) >= r.s.edge (0) + StereoSplitProcessor::softness - 0.001f);
    r.s.setEdge (0, -0.5f);
    r.s.setEdge (1, 0.2f);
    CHECK (std::abs (r.s.edge (0) + 0.5f) < 0.01f);
    CHECK (std::abs (r.s.edge (1) - 0.2f) < 0.01f);

    // A click in the middle comes out whole, the window and the Left part's 100 samples later.
    String e;
    CHECK (r.s.loadPlugin (StereoSplitProcessor::left, 0, builtIn ("test-latency:100"), e));
    const int delay = StereoSplitProcessor::fftSize + 100;
    CHECK_EQ (r.s.getLatencySamples(), delay);
    auto click = [] (int i) { return std::pair<float, float> { i == 3000 ? 1.0f : 0.0f, i == 3000 ? 1.0f : 0.0f }; };
    CHECK (worstAgainst (runStereo (r.s, click, 0.2), click, delay) < 1.0e-4);

    // Saved and opened again.
    r.s.setMode (StereoSplitProcessor::Mode::middleSides);
    CHECK (r.s.loadPlugin (StereoSplitProcessor::sides, 0, builtIn ("test-latency:50"), e));
    CHECK_EQ (r.s.getLatencySamples(), 50);
    MemoryBlock state;
    r.s.getStateInformation (state);
    StereoRig back;
    back.s.setStateInformation (state.getData(), (int) state.getSize());
    CHECK (back.s.mode() == StereoSplitProcessor::Mode::middleSides);
    CHECK (std::abs (back.s.edge (0) + 0.5f) < 0.01f);
    CHECK (back.s.part (StereoSplitProcessor::left).slots[0].plugin != nullptr);
    CHECK (back.s.part (StereoSplitProcessor::sides).slots[0].plugin != nullptr);
    CHECK_EQ (back.s.getLatencySamples(), 50);
    CHECK_EQ (back.s.partsInUse().count, 2);
}
