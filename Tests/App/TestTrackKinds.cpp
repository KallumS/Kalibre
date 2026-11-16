// Versions with their own plug-ins (decision 0050), the Summing track
// (decision 0052) and the Cue track (decision 0051), played through the live
// audio path with a stand-in sound card, and exported.
#include "Check.h"
#include "Controller.h"
#include "Edit.h"
#include "Engine.h"

using namespace kal;
using namespace juce;

namespace
{
// A sound card that's only called by hand: the engine's live path, without hardware.
struct FakeDevice : public AudioIODevice
{
    FakeDevice() : AudioIODevice ("Test", "Test") {}
    StringArray getOutputChannelNames() override { return { "L", "R" }; }
    StringArray getInputChannelNames() override { return { "L", "R" }; }
    Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
    Array<int> getAvailableBufferSizes() override { return { 512 }; }
    int getDefaultBufferSize() override { return 512; }
    String open (const BigInteger&, const BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start (AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return 512; }
    double getCurrentSampleRate() override { return 48000.0; }
    int getCurrentBitDepth() override { return 32; }
    BigInteger getActiveOutputChannels() const override { return 3; }
    BigInteger getActiveInputChannels() const override { return 3; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
};

File folder()
{
    auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("KalibreTests").getChildFile ("kinds");
    f.createDirectory();
    return f;
}

// Two seconds of silence with one full-scale click at `at` seconds.
File clickFile (double at)
{
    const auto f = folder().getChildFile ("kinds-click" + String (at) + ".wav");
    f.deleteFile();
    AudioBuffer<float> b (2, 48000 * 2);
    b.clear();
    b.setSample (0, (int) (at * 48000), 1.0f);
    b.setSample (1, (int) (at * 48000), 1.0f);
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os (f.createOutputStream().release());
    auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    return f;
}

Clip audioClip (const File& f, Tick start, Tick length)
{
    Clip c;
    c.kind = Clip::Kind::Audio;
    c.start = start;
    c.length = length;
    c.audio.file = f.getFullPathName().toStdString();
    c.audio.sourceLength = 2.0;
    return c;
}

PluginRef gain (float db)
{
    PluginRef r;
    r.format = "Kalibre";
    r.identifier = "test-gain:" + String (db).toStdString();
    r.name = "Gain";
    return r;
}

// Plays the engine's live path for `seconds`, as the sound card would; returns what it put out.
AudioBuffer<float> playLive (Engine& e, double seconds)
{
    const int n = 512, blocks = (int) std::ceil (seconds * 48000 / n);
    AudioBuffer<float> out (2, blocks * n), in (2, n), block (2, n);
    in.clear();
    for (int b = 0; b < blocks; ++b)
    {
        block.clear();
        const float* ins[] { in.getReadPointer (0), in.getReadPointer (1) };
        float* outs[] { block.getWritePointer (0), block.getWritePointer (1) };
        e.audioDeviceIOCallbackWithContext (ins, 2, outs, 2, n, {});
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, b * n, block, ch, 0, n);
    }
    return out;
}

void startLive (Engine& e, FakeDevice& device, const Project& p)
{
    e.audioDeviceAboutToStart (&device);
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    e.sync (p, 0, folder());
    // Clips' audio loads in the background: wait for it, as an export does.
    for (int tries = 0; tries < 200; ++tries)
    {
        bool waiting = false;
        for (auto& t : p.tracks)
            for (auto& c : t.clips())
                if (! c.isMidi() && ! e.cache.forClip (c, p.tempo, folder())) waiting = true;
        if (! waiting) break;
        Thread::sleep (20);
    }
    e.sync (p, 0, folder());
}

// Where the loudest sample is, and how loud.
std::pair<int, float> peak (const AudioBuffer<float>& b, int ch)
{
    int at = -1;
    float v = 0;
    for (int i = 0; i < b.getNumSamples(); ++i)
        if (std::abs (b.getSample (ch, i)) > v) { v = std::abs (b.getSample (ch, i)); at = i; }
    return { at, v };
}

AudioBuffer<float> read (const File& f)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
    AudioBuffer<float> b (2, r ? (int) r->lengthInSamples : 0);
    if (r) r->read (&b, 0, b.getNumSamples(), 0, true, true);
    return b;
}
} // namespace

TEST ("versions: each plays its own plug-ins; the others wait loaded, and come back as they were")
{
    PluginManager plugins;
    Engine engine (plugins);
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Click");
    edit::addClip (p, t, audioClip (clickFile (0.5), 0, PPQ * 4));
    p.track (t)->pan = 0;
    const Id fx = edit::addDevice (p, t, gain (0), DeviceRole::AudioFx);
    edit::newVersion (p, t);
    p.track (t)->devices.front().plugin = gain (-20);   // version 2: the same place, another plug-in
    const Id v1 = p.track (t)->versions[0].id, v2 = p.track (t)->versions[1].id;

    engine.sync (p, 0, folder());
    auto* second = engine.instance (fx);
    CHECK (second != nullptr);
    CHECK (! engine.stateOf (fx, v1).empty() || engine.stateOf (fx, v1).empty());   // version 1's is loaded too
    edit::selectVersion (p, t, 0);
    engine.sync (p, 0, folder());
    auto* first = engine.instance (fx);
    CHECK (first != nullptr && first != second);
    edit::selectVersion (p, t, 1);
    engine.sync (p, 0, folder());
    CHECK (engine.instance (fx) == second);   // kept, not made again: choosing is instant
    (void) v2;

    // Each version is heard through its own plug-in.
    Engine::RenderOptions o;
    o.start = 0; o.end = 1.0; o.tail = 0; o.dither = false; o.bitDepth = 32;
    String error;
    const auto out2 = folder().getChildFile ("v2.wav");
    CHECK (engine.render (p, folder(), out2, o, {}, error));
    edit::selectVersion (p, t, 0);
    const auto out1 = folder().getChildFile ("v1.wav");
    CHECK (engine.render (p, folder(), out1, o, {}, error));
    const float loud = peak (read (out1), 0).second, quiet = peak (read (out2), 0).second;
    CHECK (loud > 0.3f);
    CHECK (std::abs (Decibels::gainToDecibels (quiet / loud) + 20.0f) < 0.5f);
}

TEST ("cue: its plug-ins, pan and mono are heard live, and left out of an export")
{
    PluginManager plugins;
    Engine engine (plugins);
    FakeDevice device;
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Click");
    edit::addClip (p, t, audioClip (clickFile (0.25), 0, PPQ * 4));
    p.track (t)->pan = 0;
    const Id cue = edit::addCue (p);
    edit::addDevice (p, cue, gain (-12), DeviceRole::AudioFx);
    auto* c = p.track (cue);
    c->pan = -1.0;    // all to the left
    c->mono = true;

    startLive (engine, device, p);
    engine.locate (0);
    engine.play();
    const auto live = playLive (engine, 0.5);
    // Playing goes forward (it used to jump back to -1 s after the first block).
    CHECK (engine.position() > 0.49);
    engine.stop();
    const float liveL = peak (live, 0).second, liveR = peak (live, 1).second;

    Engine::RenderOptions o;
    o.start = 0; o.end = 0.5; o.tail = 0; o.dither = false; o.bitDepth = 32;
    String error;
    const auto out = folder().getChildFile ("cue-export.wav");
    CHECK (engine.render (p, folder(), out, o, {}, error));
    const auto exported = read (out);
    const float expL = peak (exported, 0).second, expR = peak (exported, 1).second;

    // Exported: the click as the mix has it, both sides.
    CHECK (expL > 0.3f);
    CHECK (std::abs (expL - expR) < 0.01f);
    // Heard: 12 dB down, and only on the left.
    CHECK (liveL > 0.01f);
    CHECK (liveR < liveL * 0.05f);
    CHECK (liveL < expL);
    // Without the Cue track, what's heard is what's exported.
    edit::removeTrack (p, cue);
    engine.sync (p, 0, folder());
    engine.locate (0);
    engine.play();
    const auto plain = playLive (engine, 0.5);
    engine.stop();
    CHECK (std::abs (peak (plain, 0).second - expL) < 0.01f);
}

TEST ("summing: armed, it records the master from where recording starts, in time; not armed, nothing")
{
    PluginManager plugins;
    Engine engine (plugins);
    FakeDevice device;
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Click");
    // The click is 1.25 s into the song, behind a plug-in that delays it 300 samples (made up for by the engine).
    edit::addClip (c.project, t, audioClip (clickFile (0.25), c.project.tempo.tickAt (1.0), PPQ * 8));
    edit::addDevice (c.project, t, PluginRef { "Kalibre", "test-latency:300", "Latency" }, DeviceRole::AudioFx);
    c.project.track (t)->pan = 0;
    const Id sum = edit::addTrack (c.project, TrackKind::Summing, "Summing");
    c.project.track (sum)->armed = true;
    c.focusTrack = sum;
    c.refresh();
    startLive (engine, device, c.project);
    c.refresh();

    // Recording starts at 0.5 s.
    c.cursor = c.project.tempo.tickAt (0.5);
    c.record();
    playLive (engine, 1.5);
    c.stop();

    const auto* s = c.project.track (sum);
    CHECK_EQ (s->clips().size(), size_t (1));
    if (s->clips().empty()) return;
    const auto& clip = s->clips().front();
    CHECK_EQ (clip.start, c.project.tempo.tickAt (0.5));
    const auto recorded = read (File (String (clip.audio.file)));
    const auto [at, v] = peak (recorded, 0);
    CHECK (v > 0.3f);
    // The click is at 1.25 s in the song: 0.75 s into the take, past the
    // moments before recording began, which the take leaves out.
    CHECK (std::abs (at - (int) ((0.75 + clip.audio.sourceStart) * 48000)) <= 2);
    CHECK (std::abs (clip.audio.sourceStart * 48000 - 300) <= 1);
    // The click track wasn't armed: nothing new on it.
    CHECK_EQ (c.project.track (t)->clips().size(), size_t (1));

    // Disarmed, recording another track leaves it alone.
    c.project.track (sum)->armed = false;
    const Id other = edit::addTrack (c.project, TrackKind::Track, "Vocal");
    c.project.track (other)->armed = true;
    c.refresh();
    c.cursor = 0;
    c.record();
    playLive (engine, 0.5);
    c.stop();
    CHECK_EQ (c.project.track (sum)->clips().size(), size_t (1));
}

TEST ("versions: choosing a number switches every chosen track that has it, and leaves the others")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id a = edit::addTrack (c.project, TrackKind::Track, "A");
    const Id b = edit::addTrack (c.project, TrackKind::Track, "B");
    const Id d = edit::addTrack (c.project, TrackKind::Track, "C");
    c.refresh();
    // A new track is on version 1; a new version is the one chosen.
    CHECK_EQ (c.project.track (a)->activeVersion, 0);
    c.newVersion (a);
    c.newVersion (b);
    CHECK_EQ (c.project.track (a)->activeVersion, 1);
    // Version 1 on all three; then 2, which C hasn't got.
    c.chooseVersion ({ a, b, d }, 0);
    CHECK_EQ (c.project.track (a)->activeVersion, 0);
    CHECK_EQ (c.project.track (b)->activeVersion, 0);
    c.chooseVersion ({ a, b, d }, 1);
    CHECK_EQ (c.project.track (a)->activeVersion, 1);
    CHECK_EQ (c.project.track (b)->activeVersion, 1);
    CHECK_EQ (c.project.track (d)->activeVersion, 0);
    CHECK (c.message.contains ("no version 2"));
    // One step to undo.
    c.undo();
    CHECK_EQ (c.project.track (a)->activeVersion, 0);
    CHECK_EQ (c.project.track (b)->activeVersion, 0);
    // A fifth version isn't made.
    c.newVersion (a); c.newVersion (a); c.newVersion (a);
    CHECK_EQ (c.project.track (a)->versions.size(), size_t (4));
    c.newVersion (a);
    CHECK_EQ (c.project.track (a)->versions.size(), size_t (4));
    CHECK (c.message.contains ("four versions at most"));
}
