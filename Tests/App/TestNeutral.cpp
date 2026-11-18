// Neutral operations (the audio engine's document, decision 0081): what
// Kalibre does that never changes the sound, each proved by a render that
// cancels exactly against what went in; and the few rules for what does.
#include "Check.h"
#include "Controller.h"
#include "Edit.h"
#include "Engine.h"
#include "JsfxDevice.h"
#include "Playback.h"

#include <random>

using namespace kal;
using namespace juce;

namespace
{
File folder()
{
    auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("KalibreTests").getChildFile ("Neutral");
    f.createDirectory();
    return f;
}

constexpr double rate = 48000;

enum class Signal { Noise, Sine, Sweep };

// A test signal at a word length: 16 or 24 bits as whole steps of that
// length, 32 as floats. One second, stereo, the right channel different.
AudioBuffer<float> make (Signal s, int bits, unsigned seed = 1, double seconds = 1.0, bool invert = false)
{
    const int n = (int) (seconds * rate);
    AudioBuffer<float> b (2, n);
    std::mt19937 rng (seed);
    const double steps = bits < 32 ? (double) (1 << (bits - 1)) : 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
        {
            double v = 0;
            switch (s)
            {
                case Signal::Noise: v = std::uniform_real_distribution<double> (-0.99, 0.99) (rng); break;
                case Signal::Sine:  v = 0.9 * std::sin (2 * MathConstants<double>::pi * (ch ? 997.0 : 440.0) * i / rate); break;
                case Signal::Sweep:
                {
                    const double t = i / rate, f0 = 20, f1 = 20000;
                    v = 0.8 * std::sin (2 * MathConstants<double>::pi * f0 * seconds / std::log (f1 / f0) * (std::pow (f1 / f0, t / seconds) - 1) + ch);
                    break;
                }
            }
            if (steps > 0) v = std::round (v * steps) / steps;   // exactly a word of that length
            b.setSample (ch, i, (float) (invert ? -v : v));
        }
    return b;
}

// Writes a WAV of exactly these values: integers for 16 and 24 bits, floats for 32.
File write (const AudioBuffer<float>& b, int bits, const String& name)
{
    const auto f = folder().getChildFile (name + ".wav");
    f.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os (f.createOutputStream().release());
    auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (bits)
                                          .withSampleFormat (bits == 32 ? AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                                        : AudioFormatWriterOptions::SampleFormat::integral));
    if (bits == 32) w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    else
    {
        // Left-justified 32-bit words, as JUCE's writers take them: no rounding of ours or theirs.
        std::vector<int> l ((size_t) b.getNumSamples()), r ((size_t) b.getNumSamples());
        const double steps = (double) (1 << (bits - 1));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            l[(size_t) i] = (int) std::llround (b.getSample (0, i) * steps) * (1 << (32 - bits));
            r[(size_t) i] = (int) std::llround (b.getSample (1, i) * steps) * (1 << (32 - bits));
        }
        const int* chans[] { l.data(), r.data(), nullptr };
        w->write (chans, b.getNumSamples());
    }
    return f;
}

AudioBuffer<float> read (const File& f, int* bits = nullptr, bool* isFloat = nullptr)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
    if (! r) return {};
    if (bits) *bits = (int) r->bitsPerSample;
    if (isFloat) *isFloat = r->usesFloatingPointData;
    AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, b.getNumSamples(), 0, true, true);
    return b;
}

Clip clipOf (const File& f, Tick start, Tick length, double seconds = 1.0)
{
    Clip c;
    c.kind = Clip::Kind::Audio;
    c.start = start;
    c.length = length;
    c.audio.file = f.getFullPathName().toStdString();
    c.audio.sourceLength = seconds;
    c.audio.fileSampleRate = rate;
    return c;
}

AudioBuffer<float> render (Engine& engine, const Project& p, double seconds, int bits = 32, bool dither = false, double start = 0)
{
    Engine::RenderOptions o;
    o.start = start; o.end = start + seconds; o.tail = 0; o.dither = dither; o.bitDepth = bits;
    const auto out = folder().getChildFile ("render.wav");
    String error;
    CHECK (engine.render (p, folder(), out, o, {}, error));
    return read (out);
}

// The largest difference between two renders over [from, from + n) of b against a at `aFrom`.
float largestDifference (const AudioBuffer<float>& a, int aFrom, const AudioBuffer<float>& b, int bFrom, int n)
{
    float worst = 0;
    if (a.getNumSamples() < aFrom + n || b.getNumSamples() < bFrom + n) return 1e9f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
            worst = jmax (worst, std::abs (a.getSample (ch, aFrom + i) - b.getSample (ch, bFrom + i)));
    return worst;
}

bool silent (const AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (b.getSample (ch, i) != 0.0f) return false;
    return b.getNumSamples() > 0;
}

Project oneTrack (const File& f, Id* track = nullptr, Id* clip = nullptr, Tick start = 0)
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Source");
    p.track (t)->pan = 0;
    const Id c = edit::addClip (p, t, clipOf (f, start, PPQ * 2));   // one second at 120
    if (track) *track = t;
    if (clip) *clip = c;
    return p;
}
} // namespace

//==============================================================================
TEST ("neutral: a render to the same or more bits cancels exactly; to fewer, each sample is the nearest step")
{
    PluginManager plugins;
    Engine engine (plugins);
    for (auto s : { Signal::Noise, Signal::Sine, Signal::Sweep })
        for (int srcBits : { 16, 24, 32 })
        {
            const auto source = make (s, srcBits, (unsigned) srcBits);
            const auto file = write (source, srcBits, "source" + String ((int) s) + "-" + String (srcBits));
            const auto p = oneTrack (file);
            for (int outBits : { 16, 24, 32 })
            {
                const auto out = render (engine, p, 1.0, outBits);
                CHECK_EQ (out.getNumSamples(), source.getNumSamples());
                if (outBits >= srcBits)
                {
                    const auto worst = largestDifference (source, 0, out, 0, source.getNumSamples());
                    if (worst != 0.0f)
                        check::fail (__FILE__, __LINE__, "signal " + std::to_string ((int) s) + ", " + std::to_string (srcBits)
                                                         + " bits to " + std::to_string (outBits) + ": off by " + std::to_string (worst));
                }
                else
                {
                    // Rounded to the nearest step of the shorter word, never truncated.
                    const double steps = (double) (1 << (outBits - 1));
                    int wrong = 0;
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < source.getNumSamples(); ++i)
                        {
                            const double want = jlimit (-steps, steps - 1, std::round ((double) source.getSample (ch, i) * steps)) / steps;
                            if ((double) out.getSample (ch, i) != want) ++wrong;
                        }
                    if (wrong != 0)
                        check::fail (__FILE__, __LINE__, "signal " + std::to_string ((int) s) + ", " + std::to_string (srcBits)
                                                         + " bits to " + std::to_string (outBits) + ": " + std::to_string (wrong) + " samples not the nearest step");
                }
            }
        }
}

TEST ("neutral: dither is never added to a 32-bit render")
{
    PluginManager plugins;
    Engine engine (plugins);
    const auto source = make (Signal::Sine, 24);
    const auto p = oneTrack (write (source, 24, "dither"));
    const auto out = render (engine, p, 1.0, 32, true);
    CHECK_EQ (largestDifference (source, 0, out, 0, source.getNumSamples()), 0.0f);
}

TEST ("neutral: a clip plays its file sample for sample, wherever it starts and however it's trimmed")
{
    PluginManager plugins;
    Engine engine (plugins);
    const auto source = make (Signal::Noise, 24, 7, 2.0);
    const auto file = write (source, 24, "placed");
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Placed");
    p.track (t)->pan = 0;
    // A beat in (0.5 s), the first quarter second trimmed off.
    auto c = clipOf (file, PPQ, PPQ * 2, 2.0);
    c.offset = PPQ / 2;
    c.audio.sourceStart = 0;
    edit::addClip (p, t, c);
    const auto out = render (engine, p, 2.0);
    // From 0.5 s, the file from 0.25 s, for the clip's second.
    CHECK_EQ (largestDifference (source, 12000, out, 24000, 48000), 0.0f);
    // Nothing before it or after it.
    float outside = 0;
    for (int i = 0; i < 24000; ++i) outside = jmax (outside, std::abs (out.getSample (0, i)));
    for (int i = 72000; i < out.getNumSamples(); ++i) outside = jmax (outside, std::abs (out.getSample (0, i)));
    CHECK_EQ (outside, 0.0f);
}

TEST ("neutral: a warped clip at its own tempo plays unstretched in every warp mode, and again after the tempo comes back")
{
    PluginManager plugins;
    Engine engine (plugins);
    const auto source = make (Signal::Sweep, 24, 3, 2.0);
    const auto file = write (source, 24, "warped");
    for (auto mode : { WarpMode::Repitch, WarpMode::Granular, WarpMode::Spectral })
    {
        Id clip = 0;
        auto p = oneTrack (file, nullptr, &clip);
        p.clip (clip)->length = PPQ * 4;
        p.clip (clip)->audio.sourceLength = 2.0;
        p.clip (clip)->audio.warp = mode;
        p.clip (clip)->audio.sourceBpm = 120;
        auto out = render (engine, p, 2.0);
        const auto worst = largestDifference (source, 0, out, 0, 96000);
        if (worst != 0.0f) check::fail (__FILE__, __LINE__, "warp mode " + std::to_string ((int) mode) + ": off by " + std::to_string (worst));

        // Slower, then back: the original tracks play as they were.
        p.tempo.setTempoAt (0, 90);
        out = render (engine, p, 2.0);
        CHECK (largestDifference (source, 0, out, 0, 96000) > 0.0f);
        p.tempo.setTempoAt (0, 120);
        out = render (engine, p, 2.0);
        CHECK_EQ (largestDifference (source, 0, out, 0, 96000), 0.0f);
    }
    // Warp markers that agree with the tempo are unstretched too.
    Id clip = 0;
    auto p = oneTrack (file, nullptr, &clip);
    p.clip (clip)->length = PPQ * 4;
    p.clip (clip)->audio.warp = WarpMode::Spectral;
    p.clip (clip)->audio.markers = { { 0.0, 0 }, { 1.0, PPQ * 2 }, { 2.0, PPQ * 4 } };
    const auto out = render (engine, p, 2.0);
    CHECK_EQ (largestDifference (source, 0, out, 0, 96000), 0.0f);
}

TEST ("neutral: eight pairs of 24-bit signals and their inverses cancel exactly at every mix point")
{
    PluginManager plugins;
    Engine engine (plugins);
    for (auto s : { Signal::Noise, Signal::Sine })
    {
        std::vector<File> plus, minus;
        for (int k = 0; k < 8; ++k)
        {
            const auto sig = s == Signal::Noise ? make (s, 24, (unsigned) (100 + k)) : make (s, 24);
            plus.push_back (write (sig, 24, "pair+" + String (k)));
            AudioBuffer<float> inv (sig);
            inv.applyGain (-1.0f);
            minus.push_back (write (inv, 24, "pair-" + String (k)));
        }
        // Every signal first, then every inverse: in 32-bit float the running sum rounds.
        auto addAll = [&] (Project& p, Id parent, std::vector<Id>* made)
        {
            for (auto* list : { &plus, &minus })
                for (auto& f : *list)
                {
                    const Id t = edit::addTrack (p, TrackKind::Track, "T", 0, parent);
                    p.track (t)->pan = 0;
                    edit::addClip (p, t, clipOf (f, 0, PPQ * 2));
                    if (made) made->push_back (t);
                }
        };
        // Into the master (the Main track).
        {
            Project p;
            addAll (p, 0, nullptr);
            CHECK (silent (render (engine, p, 1.0)));
        }
        // Into a folder.
        {
            Project p;
            const Id f = edit::addTrack (p, TrackKind::Folder, "Folder");
            addAll (p, f, nullptr);
            CHECK (silent (render (engine, p, 1.0)));
        }
        // Into a return, through sends at 0 dB; the tracks' own faders down.
        {
            Project p;
            const Id ret = edit::addTrack (p, TrackKind::Return, "Return");
            std::vector<Id> made;
            addAll (p, 0, &made);
            for (Id t : made)
            {
                CHECK (edit::addSend (p, t, ret, 0.0));
                p.track (t)->sends.back().preFader = true;
                p.track (t)->volumeDb = -100;   // silent
            }
            CHECK (silent (render (engine, p, 1.0)));
        }
        // Into one track: every clip on its own track would be the same; here,
        // a track's instruments and its clips meet at its input. Two tracks of
        // the same clip and one of its inverse at +6 dB do too.
        {
            Project p;
            for (int k = 0; k < 2; ++k)
            {
                const Id t = edit::addTrack (p, TrackKind::Track, "Twice");
                p.track (t)->pan = 0;
                edit::addClip (p, t, clipOf (plus[0], 0, PPQ * 2));
            }
            const Id t = edit::addTrack (p, TrackKind::Track, "Inverse, doubled");
            p.track (t)->pan = 0;
            p.track (t)->volumeDb = 20.0 * std::log10 (2.0);
            edit::addClip (p, t, clipOf (minus[0], 0, PPQ * 2));
            const auto out = render (engine, p, 1.0);
            float worst = 0;
            for (int i = 0; i < out.getNumSamples(); ++i) worst = jmax (worst, std::abs (out.getSample (0, i)));
            CHECK (worst < 1.0e-6f);   // the +6 dB is a gain change: near, not exact
        }
    }
}

TEST ("neutral: through a folder, or a pre-fader send to a return, a track's sound arrives unchanged")
{
    PluginManager plugins;
    Engine engine (plugins);
    const auto source = make (Signal::Noise, 24, 11);
    const auto file = write (source, 24, "routed");
    {
        Project p;
        const Id outer = edit::addTrack (p, TrackKind::Folder, "Outer");
        const Id inner = edit::addTrack (p, TrackKind::Folder, "Inner", 0, outer);
        const Id t = edit::addTrack (p, TrackKind::Track, "Deep", 0, inner);
        p.track (t)->pan = p.track (inner)->pan = p.track (outer)->pan = 0;
        edit::addClip (p, t, clipOf (file, 0, PPQ * 2));
        CHECK_EQ (largestDifference (source, 0, render (engine, p, 1.0), 0, 48000), 0.0f);
    }
    {
        Project p;
        const Id ret = edit::addTrack (p, TrackKind::Return, "Return");
        const Id t = edit::addTrack (p, TrackKind::Track, "Sent");
        p.track (t)->pan = p.track (ret)->pan = 0;
        edit::addClip (p, t, clipOf (file, 0, PPQ * 2));
        CHECK (edit::addSend (p, t, ret, 0.0));
        p.track (t)->sends.back().preFader = true;
        p.track (t)->volumeDb = -100;
        CHECK_EQ (largestDifference (source, 0, render (engine, p, 1.0), 0, 48000), 0.0f);
    }
}

TEST ("neutral: panning is constant power with sine curves: 0 dB in the middle, +3 dB fully to one side")
{
    PluginManager plugins;
    Engine engine (plugins);
    const auto source = make (Signal::Noise, 24, 5);
    Id t = 0;
    auto p = oneTrack (write (source, 24, "panned"), &t);
    for (double pan : { -1.0, -0.5, 0.0, 0.3, 1.0 })
    {
        p.track (t)->pan = pan;
        const auto out = render (engine, p, 1.0);
        const double angle = (pan + 1) * MathConstants<double>::pi / 4;
        const double gl = pan == 0 ? 1.0 : std::sqrt (2.0) * std::cos (angle);
        const double gr = pan == 0 ? 1.0 : std::sqrt (2.0) * std::sin (angle);
        double worst = 0;
        for (int i = 0; i < 48000; ++i)
        {
            worst = std::max (worst, std::abs (out.getSample (0, i) - source.getSample (0, i) * gl));
            worst = std::max (worst, std::abs (out.getSample (1, i) - source.getSample (1, i) * gr));
        }
        if (pan == 0) CHECK_EQ (worst, 0.0);
        else CHECK (worst < 1.0e-6);
    }
}

TEST ("neutral: a bypassed effect is out of the signal flow, and still delays as much as it would playing")
{
    PluginManager plugins;
    Engine engine (plugins);
    const auto source = make (Signal::Noise, 24, 9);
    const auto file = write (source, 24, "bypassed");
    std::vector<PluginRef> effects { { "Kalibre", "xy-morph", "X/Y Morph" }, { "Kalibre", "container", "Container" },
                                     { "Kalibre", "test-gain:0.5", "Gain" }, { "Kalibre", "test-latency:300", "Latency" } };
    for (auto& spec : jsfxDevices())
        if (spec.role == DeviceRole::AudioFx) effects.push_back (jsfxRef (spec));
    CHECK (effects.size() >= 5);
    for (auto& ref : effects)
    {
        Id t = 0;
        auto p = oneTrack (file, &t);
        const Id d = edit::addDevice (p, t, ref, DeviceRole::AudioFx);
        p.device (d)->bypassed = true;
        const auto worst = largestDifference (source, 0, render (engine, p, 1.0), 0, 48000);
        if (worst != 0.0f) check::fail (__FILE__, __LINE__, ref.name + " bypassed: off by " + std::to_string (worst));
    }
    // A click through a bypassed 300-sample plug-in still lines up with one that isn't delayed.
    AudioBuffer<float> click (2, 48000);
    click.clear();
    click.setSample (0, 12000, 0.5f);
    click.setSample (1, 12000, 0.5f);
    const auto clickFile = write (click, 32, "bypass-click");
    Project p;
    const Id a = edit::addTrack (p, TrackKind::Track, "Dry");
    const Id b = edit::addTrack (p, TrackKind::Track, "Bypassed");
    p.track (a)->pan = p.track (b)->pan = 0;
    edit::addClip (p, a, clipOf (clickFile, 0, PPQ * 2));
    edit::addClip (p, b, clipOf (clickFile, 0, PPQ * 2));
    p.device (edit::addDevice (p, b, { "Kalibre", "test-latency:300", "Latency" }, DeviceRole::AudioFx))->bypassed = true;
    const auto out = render (engine, p, 1.0);
    int clicks = 0;
    for (int i = 0; i < out.getNumSamples(); ++i) if (std::abs (out.getSample (0, i)) > 0.1f) ++clicks;
    CHECK_EQ (clicks, 1);
    CHECK_EQ (out.getSample (0, 12000), 1.0f);
}

TEST ("neutral: a split clip plays exactly as the whole one, warped or not, looped or not")
{
    PluginManager plugins;
    Engine engine (plugins);
    const auto source = make (Signal::Noise, 24, 21, 2.0);
    const auto file = write (source, 24, "split");
    for (bool warped : { false, true })
        for (bool looped : { false, true })
        {
            auto build = [&] (std::vector<Tick> cuts)
            {
                Project p;
                const Id t = edit::addTrack (p, TrackKind::Track, "Split");
                p.track (t)->pan = 0;
                auto c = clipOf (file, 0, PPQ * 3, 2.0);   // 1.5 s of a 2 s file
                if (warped) { c.audio.warp = WarpMode::Repitch; c.audio.sourceBpm = 120; }
                const Id id = edit::addClip (p, t, c);
                if (looped) edit::loopContent (p, id);    // 1.5 s twice
                Id right = id;
                for (Tick at : cuts) right = edit::splitClip (p, right, at);
                return p;
            };
            const auto whole = render (engine, build ({}), 3.5);
            // On beats, between ticks' samples, and at the loop's own seam.
            const auto split = render (engine, build ({ PPQ + 7, PPQ * 3, PPQ * 4 + 333 }), 3.5);
            const auto worst = largestDifference (whole, 0, split, 0, whole.getNumSamples());
            if (worst != 0.0f)
                check::fail (__FILE__, __LINE__, std::string (warped ? "warped" : "unwarped") + (looped ? ", looped" : "")
                                                 + ": off by " + std::to_string (worst));
            if (looped)
            {
                // The second pass is the first again.
                CHECK_EQ (largestDifference (whole, 0, whole, 72000, 72000), 0.0f);
                CHECK_EQ (largestDifference (source, 0, whole, 0, 72000), 0.0f);
            }
        }
}

TEST ("neutral: volume automation is followed sample by sample")
{
    PluginManager plugins;
    Engine engine (plugins);
    AudioBuffer<float> dc (2, 96000);
    for (int ch = 0; ch < 2; ++ch) FloatVectorOperations::fill (dc.getWritePointer (ch), 0.5f, dc.getNumSamples());
    Id t = 0;
    auto p = oneTrack (write (dc, 32, "dc"), &t);
    p.track (t)->clips().front().length = PPQ * 4;
    p.track (t)->clips().front().audio.sourceLength = 2.0;
    // From -30 dB to +6 dB over the first second (the lane runs -60..+6 dB).
    AutomationLane lane;
    lane.id = p.newId();
    lane.target.kind = AutoTarget::Kind::Volume;
    lane.points = { { 0, 30.0 / 66.0 }, { PPQ * 2, 1.0 } };
    p.track (t)->automation.push_back (lane);
    const auto out = render (engine, p, 2.0);
    double worstDb = 0;
    for (int i = 1; i < 48000; ++i)
    {
        const double v = 30.0 / 66.0 + (1.0 - 30.0 / 66.0) * (i / 48000.0);
        const double want = 0.5 * std::pow (10.0, (v * 66.0 - 60.0) / 20.0);
        worstDb = std::max (worstDb, std::abs (20 * std::log10 (out.getSample (0, i) / want)));
    }
    CHECK (worstDb < 0.01);
    // And held at the last point's level after it.
    CHECK (std::abs (out.getSample (0, 60000) - 0.5f * 2.0f) < 0.01f);
}

TEST ("neutral: a frozen track is its sound exactly, as 32-bit float, with its tail")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    const auto source = make (Signal::Noise, 24, 31);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Frozen");
    edit::addClip (c.project, t, clipOf (write (source, 24, "freeze"), 0, PPQ * 2));
    c.refresh();
    c.freeze (t, true);
    int bits = 0;
    bool isFloat = false;
    const auto frozen = read (File (c.project.track (t)->frozenFile), &bits, &isFloat);
    CHECK_EQ (bits, 32);
    CHECK (isFloat);
    CHECK (frozen.getNumSamples() > 48000);   // past the clip's end: room for a tail
    CHECK_EQ (largestDifference (source, 0, frozen, 0, 48000), 0.0f);
    c.freeze (t, false);
}

TEST ("not neutral: consolidating audio clips makes a new file, normalised, the clip's gain making up for it")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    auto quiet = make (Signal::Sine, 24, 1, 1.0);
    quiet.applyGain (0.25f);
    const auto file = write (quiet, 32, "consolidate");
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Two Clips");
    c.project.track (t)->pan = 0;
    const Id a = edit::addClip (c.project, t, clipOf (file, 0, PPQ));
    const Id b = edit::addClip (c.project, t, clipOf (file, PPQ * 2, PPQ));
    c.project.clip (b)->audio.gainDb = -6;
    c.refresh();
    const auto before = render (engine, c.project, 2.0);
    c.consolidate ({ a, b });
    const auto& clips = c.project.track (t)->clips();
    CHECK_EQ (clips.size(), size_t (1));
    if (clips.size() != 1) return;
    const auto& made = clips.front();
    CHECK_EQ (made.start, Tick (0));
    CHECK_EQ (made.length, PPQ * 3);
    // The file peaks at 0 dBFS; the clip is turned down to where the loudest was (0.9 x 0.25).
    const auto written = read (File (made.audio.file));
    CHECK (std::abs (written.getMagnitude (0, written.getNumSamples()) - 1.0f) < 1.0e-6f);
    CHECK (std::abs (made.audio.gainDb - 20 * std::log10 (0.9 * 0.25)) < 0.01);
    // A gain change each way: close, not exact.
    const auto after = render (engine, c.project, 2.0);
    CHECK (largestDifference (before, 0, after, 0, 96000) < 1.0e-6f);
    CHECK (c.undo());
    CHECK_EQ (c.project.track (t)->clips().size(), size_t (2));
}
