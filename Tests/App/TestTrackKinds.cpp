// Versions with their own plug-ins (decision 0050), the Summing track
// (decision 0052) and the Cue track (decision 0051), played through the live
// audio path with a stand-in sound card, and exported.
#include "Check.h"
#include "Controller.h"
#include "Edit.h"
#include "Engine.h"
#include "ProjectFile.h"
#include "PluginManager.h"
#include "BuiltinInstruments.h"

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

TEST ("versions: each plays its own plug-ins; the others are unloaded, and come back as they were (0103)")
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
    CHECK (engine.stateOf (fx, v1).empty());   // version 1's isn't loaded (0103)
    edit::selectVersion (p, t, 0);
    engine.sync (p, 0, folder());
    auto* first = engine.instance (fx);
    CHECK (first != nullptr && first != second);
    edit::selectVersion (p, t, 1);
    engine.sync (p, 0, folder());
    CHECK (engine.instance (fx) != nullptr);
    CHECK (engine.stateOf (fx, v1).empty());   // version 1's let go again
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

TEST ("recording with a count-in: the take starts where recording starts, not a bar early, and holds no count-in (UAT 23 November)")
{
    // The UAT: with Count-in on, a Summing take began with a silent bar and
    // the song a bar late in it.
    PluginManager plugins;
    Engine engine (plugins);
    FakeDevice device;
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Click");
    edit::addClip (c.project, t, audioClip (clickFile (0.25), c.project.tempo.tickAt (1.0), PPQ * 8));
    c.project.track (t)->pan = 0;
    const Id sum = edit::addTrack (c.project, TrackKind::Summing, "Summing");
    c.project.track (sum)->armed = true;
    c.project.metronome.countIn = true;   // a bar of clicks first: 2 s at 120 BPM
    c.project.metronome.on = false;
    c.focusTrack = sum;
    c.refresh();
    startLive (engine, device, c.project);
    c.refresh();

    c.cursor = 0;
    c.record();
    playLive (engine, 2.0 + 2.0);
    c.stop();

    const auto* s = c.project.track (sum);
    CHECK_EQ (s->clips().size(), size_t (1));
    if (s->clips().empty()) return;
    const auto& clip = s->clips().front();
    CHECK (clip.start >= 0 && clip.start < c.project.tempo.tickAt (0.01));
    const auto recorded = read (File (String (clip.audio.file)));
    const auto [at, v] = peak (recorded, 0);
    CHECK (v > 0.3f);
    // The click is 1.25 s into the song: 1.25 s into the take (from where it's read).
    CHECK (std::abs (at - (int) ((1.25 - c.project.tempo.secondsAt (clip.start) + clip.audio.sourceStart) * 48000)) <= 2);
}

TEST ("recording: the take grows on the armed track as it's recorded, from where recording starts, none during the count-in (UAT)")
{
    PluginManager plugins;
    Engine engine (plugins);
    FakeDevice device;
    Controller c (engine);
    c.project = Project();
    const Id sum = edit::addTrack (c.project, TrackKind::Summing, "Summing");
    c.project.track (sum)->armed = true;
    c.project.metronome.countIn = true;   // a bar of clicks first: 2 s at 120 BPM
    c.project.metronome.on = false;
    c.focusTrack = sum;
    c.refresh();
    startLive (engine, device, c.project);
    c.refresh();
    CHECK (c.recordingTake().first < 0);
    c.cursor = PPQ * 4;
    c.record();
    playLive (engine, 1.0);
    CHECK (c.recordingTake().first < 0);                  // counting in
    playLive (engine, 2.0);
    const auto [from, to] = c.recordingTake();
    CHECK_EQ (from, PPQ * 4);
    CHECK (std::abs (to - (PPQ * 4 + PPQ * 2)) <= PPQ / 4);   // a second in: two beats
    c.stop();
    CHECK (c.recordingTake().first < 0);
}

TEST ("recording MIDI: a track armed for MIDI records what's played in, not its own clips playing (UAT 23 November)")
{
    // The UAT: armed for MIDI with nothing played, recording laid copies of
    // the track's own clip over it, every note doubled.
    PluginManager plugins;
    Engine engine (plugins);
    FakeDevice device;
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Lead");
    edit::addDevice (c.project, t, synthRef(), DeviceRole::Instrument);
    Clip clip;
    clip.kind = Clip::Kind::Midi;
    clip.start = 0;
    clip.length = PPQ * 4;
    for (int i = 0; i < 4; ++i)
    {
        Note n;
        n.id = 100 + i;
        n.start = i * PPQ;
        n.length = PPQ / 2;
        n.pitch = 60 + i;
        clip.midi.notes.push_back (n);
    }
    edit::addClip (c.project, t, clip);
    c.project.track (t)->armed = true;
    c.project.track (t)->armFor = ArmFor::Midi;
    c.focusTrack = t;
    c.refresh();
    startLive (engine, device, c.project);
    c.refresh();

    c.cursor = 0;
    c.record();
    playLive (engine, 2.5);
    c.stop();
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK_EQ (c.project.track (t)->clips().size(), size_t (1));
}

TEST ("summing: recording the master at 32 bits is neutral: the take is the song's sound, sample for sample (0081)")
{
    PluginManager plugins;
    Engine engine (plugins);
    FakeDevice device;
    Controller c (engine);
    c.project = Project();
    // A second of 24-bit noise, half a second into the song.
    AudioBuffer<float> noise (2, 48000);
    Random rng (5);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < noise.getNumSamples(); ++i)
            noise.setSample (ch, i, (float) std::round ((rng.nextFloat() * 1.8f - 0.9f) * 8388608.0f) / 8388608.0f);
    const auto file = folder().getChildFile ("kinds-noise.wav");
    file.deleteFile();
    {
        WavAudioFormat wav;
        std::unique_ptr<OutputStream> os (file.createOutputStream().release());
        auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
        w->writeFromAudioSampleBuffer (noise, 0, noise.getNumSamples());
    }
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Noise");
    auto clip = audioClip (file, PPQ, PPQ * 2);
    clip.audio.sourceLength = 1.0;
    edit::addClip (c.project, t, clip);
    c.project.track (t)->pan = 0;
    const Id sum = edit::addTrack (c.project, TrackKind::Summing, "Summing");
    c.project.track (sum)->armed = true;
    c.focusTrack = sum;
    c.refresh();
    startLive (engine, device, c.project);
    c.refresh();
    c.cursor = 0;
    c.record();
    playLive (engine, 2.0);
    c.stop();

    const auto* s = c.project.track (sum);
    CHECK_EQ (s->clips().size(), size_t (1));
    if (s->clips().empty()) return;
    const auto& take = s->clips().front();
    const auto recorded = read (File (String (take.audio.file)));
    const int at = (int) std::llround ((take.audio.sourceStart + 0.5) * 48000);
    float worst = recorded.getNumSamples() >= at + 48000 ? 0.0f : 1.0f;
    for (int ch = 0; ch < 2 && worst == 0.0f; ++ch)
        for (int i = 0; i < 48000; ++i)
            worst = jmax (worst, std::abs (recorded.getSample (ch, at + i) - noise.getSample (ch, i)));
    CHECK_EQ (worst, 0.0f);
}

TEST ("recording: what the interface sends is written as it came, sample for sample, at 32-bit float (0081)")
{
    PluginManager plugins;
    Engine engine (plugins);
    FakeDevice device;
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Vocal");
    c.project.track (t)->armed = true;
    c.focusTrack = t;
    c.refresh();
    startLive (engine, device, c.project);
    c.refresh();
    c.cursor = 0;
    c.record();
    // A converter's 24-bit words, coming in for a second.
    const int n = 512, blocks = 94;
    AudioBuffer<float> sent (2, n * blocks), out (2, n);
    Random rng (9);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < sent.getNumSamples(); ++i)
            sent.setSample (ch, i, (float) std::round ((rng.nextFloat() * 1.9f - 0.95f) * 8388608.0f) / 8388608.0f);
    for (int b = 0; b < blocks; ++b)
    {
        const float* ins[] { sent.getReadPointer (0, b * n), sent.getReadPointer (1, b * n) };
        float* outs[] { out.getWritePointer (0), out.getWritePointer (1) };
        engine.audioDeviceIOCallbackWithContext (ins, 2, outs, 2, n, {});
    }
    c.stop();
    const auto* tr = c.project.track (t);
    CHECK_EQ (tr->clips().size(), size_t (1));
    if (tr->clips().empty()) return;
    const File taken (String (tr->clips().front().audio.file));
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (taken));
    CHECK (r != nullptr);
    if (! r) return;
    CHECK_EQ ((int) r->bitsPerSample, 32);
    CHECK (r->usesFloatingPointData);
    const auto got = read (taken);
    CHECK (got.getNumSamples() >= sent.getNumSamples() - 4 * n);
    // Where the take begins in what was sent (recording starts with the next block).
    int from = -1;
    for (int d = 0; d < 4 * n && from < 0; ++d)
        if (got.getSample (0, 0) == sent.getSample (0, d) && got.getSample (0, 1) == sent.getSample (0, d + 1)) from = d;
    CHECK (from >= 0);
    float worst = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i + from < sent.getNumSamples() && i < got.getNumSamples(); ++i)
            worst = jmax (worst, std::abs (got.getSample (ch, i) - sent.getSample (ch, i + from)));
    CHECK_EQ (worst, 0.0f);
}

TEST ("versions: a version not playing keeps its plug-ins' settings, not the plug-ins; Undo keeps what was changed since (0103)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Keys");
    const Id synth = edit::addDevice (c.project, t, synthRef(), DeviceRole::Instrument);
    c.refresh();
    auto setting = [&]() -> float
    {
        auto* inst = engine.instance (synth);
        return inst ? inst->getParameters()[3]->getValue() : -1.0f;   // Osc 2 Fine, a knob that goes smoothly
    };
    auto set = [&] (float v) { if (auto* inst = engine.instance (synth)) inst->getParameters()[3]->setValueNotifyingHost (v); };
    set (0.73f);
    const float first = setting();
    c.newVersion (t);   // version 2, chosen, a copy
    const Id v1 = c.project.track (t)->versions[0].id;
    CHECK (std::abs (setting() - first) < 0.001f);
    CHECK (engine.stateOf (synth, v1).empty());   // version 1's synth isn't in memory
    set (0.21f);
    const float second = setting();
    c.chooseVersion ({ t }, 0);
    CHECK (std::abs (setting() - first) < 0.001f);
    c.chooseVersion ({ t }, 1);
    CHECK (std::abs (setting() - second) < 0.001f);
    // Changed on version 2, then Undo back to version 1: the change waits in version 2.
    set (0.4f);
    const float changed = setting();
    c.undo();
    CHECK_EQ (c.project.track (t)->activeVersion, 0);
    CHECK (std::abs (setting() - first) < 0.001f);
    c.redo();
    CHECK_EQ (c.project.track (t)->activeVersion, 1);
    CHECK (std::abs (setting() - changed) < 0.001f);
    c.undo();
    c.chooseVersion ({ t }, 1);
    CHECK (std::abs (setting() - changed) < 0.001f);
}

TEST ("song versions: the one not playing has no plug-ins loaded and no audio in memory; choosing it brings them back as they were, as one undo step (0103)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id keys = edit::addTrack (c.project, TrackKind::Track, "Keys");
    const Id synth = edit::addDevice (c.project, keys, synthRef(), DeviceRole::Instrument);
    const Id drums = edit::addTrack (c.project, TrackKind::Track, "Drums");
    edit::addClip (c.project, drums, audioClip (clickFile (0.5), 0, PPQ * 4));
    const Id pad = edit::addTrack (c.project, TrackKind::Track, "Pad");
    edit::addDevice (c.project, pad, synthRef(), DeviceRole::Instrument);
    c.refresh();
    auto waitForAudio = [&]
    {
        for (int i = 0; i < 100 && engine.cache.bytesHeld() == 0; ++i) MessageManager::getInstance()->runDispatchLoopUntil (50);
        return engine.cache.bytesHeld() > 0;
    };
    auto setting = [&]() -> float
    {
        auto* inst = engine.instance (synth);
        return inst ? inst->getParameters()[3]->getValue() : -1.0f;
    };
    auto set = [&] (float v) { if (auto* inst = engine.instance (synth)) inst->getParameters()[3]->setValueNotifyingHost (v); };
    CHECK (waitForAudio());
    CHECK_EQ (engine.loadedPlugins(), 2);
    set (0.73f);
    const float first = setting();

    // Version 2: a copy, chosen. Nothing loads again to make it.
    auto* before = engine.instance (synth);
    c.newSongVersion();
    CHECK_EQ (c.project.songVersions(), 2);
    CHECK_EQ (c.project.songVersion, 1);
    CHECK (engine.instance (synth) == before);
    // Version 2 without the drums and the pad: their plug-in and audio let go of.
    c.apply ("Delete", [&] (Project& p) { edit::removeTrack (p, drums); edit::removeTrack (p, pad); });
    CHECK_EQ (engine.loadedPlugins(), 1);
    CHECK_EQ (engine.cache.bytesHeld(), (size_t) 0);
    set (0.21f);
    const float second = setting();

    // Version 1: its plug-ins made again from their settings, its audio read again.
    engine.play();
    c.chooseSongVersion (0);
    CHECK (engine.isPlaying());   // it plays on
    engine.stop();
    CHECK_EQ (c.project.songVersion, 0);
    CHECK_EQ (c.project.tracks.size(), (size_t) 3);
    CHECK_EQ (engine.loadedPlugins(), 2);
    CHECK (std::abs (setting() - first) < 0.001f);
    CHECK (waitForAudio());

    // Version 2 again: version 1's two synths and its audio gone from memory.
    c.chooseSongVersion (1);
    CHECK_EQ (engine.loadedPlugins(), 1);
    CHECK_EQ (engine.cache.bytesHeld(), (size_t) 0);
    CHECK (std::abs (setting() - second) < 0.001f);

    // One undo step; what was changed on the version left since goes with it.
    set (0.4f);
    const float changed = setting();
    c.undo();
    CHECK_EQ (c.project.songVersion, 0);
    CHECK (std::abs (setting() - first) < 0.001f);
    c.chooseSongVersion (1);
    CHECK (std::abs (setting() - changed) < 0.001f);

    // Saved and opened again: both versions there.
    const auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("song-versions.kalibre");
    CHECK (c.saveAs (f));
    CHECK (c.open (f));
    CHECK_EQ (c.project.songVersions(), 2);
    CHECK_EQ (c.project.songVersion, 1);
    CHECK (std::abs (setting() - changed) < 0.001f);
    c.chooseSongVersion (0);
    CHECK (std::abs (setting() - first) < 0.001f);
    f.deleteFile();
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

TEST ("choosing: Shift-click a range of tracks, Option-click adds, Command-click takes away, the same in any view's order")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    std::vector<Id> t;
    for (int i = 0; i < 5; ++i) t.push_back (edit::addTrack (c.project, TrackKind::Track, "T" + std::to_string (i)));
    c.refresh();
    const ModifierKeys none, shift (ModifierKeys::shiftModifier), option (ModifierKeys::altModifier), cmd (ModifierKeys::commandModifier);
    const auto order = c.arrangeOrder();
    auto chosen = [&c] { auto s = c.selectedTracks; std::sort (s.begin(), s.end()); return s; };

    c.chooseTrack (t[1], none, order);
    c.chooseTrack (t[3], shift, order);
    CHECK (chosen() == (std::vector<Id> { t[1], t[2], t[3] }));
    CHECK_EQ (c.focusTrack, t[3]);
    // Backwards from the same anchor.
    c.chooseTrack (t[0], shift, order);
    CHECK (chosen() == (std::vector<Id> { t[0], t[1] }));
    // Option adds, without losing what's chosen.
    c.chooseTrack (t[4], option, order);
    CHECK (chosen() == (std::vector<Id> { t[0], t[1], t[4] }));
    // Shift now runs from the last one Option-clicked.
    c.chooseTrack (t[2], shift, order);
    CHECK (chosen() == (std::vector<Id> { t[2], t[3], t[4] }));
    // Command takes one away.
    c.chooseTrack (t[3], cmd, order);
    CHECK (chosen() == (std::vector<Id> { t[2], t[4] }));
    // Shift-Option takes one out.
    c.chooseTrack (t[4], ModifierKeys (ModifierKeys::shiftModifier | ModifierKeys::altModifier), order);
    CHECK (chosen() == (std::vector<Id> { t[2] }));
    // A plain click chooses only that one.
    c.chooseTrack (t[0], none, order);
    CHECK (chosen() == (std::vector<Id> { t[0] }));
    // The mixer's order ends with the master: a range can reach it.
    auto mixer = order;
    mixer.push_back (c.project.master.id);
    c.chooseTrack (t[4], none, mixer);
    c.chooseTrack (c.project.master.id, shift, mixer);
    CHECK_EQ (c.selectedTracks.size(), size_t (2));
}

TEST ("choosing: Shift-click clips takes every clip between the two, across tracks; Option-click adds one")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id a = edit::addTrack (c.project, TrackKind::Track, "A");
    const Id b = edit::addTrack (c.project, TrackKind::Track, "B");
    const Id d = edit::addTrack (c.project, TrackKind::Track, "C");
    const Id a1 = edit::addMidiClip (c.project, a, 0, PPQ * 4);
    const Id a2 = edit::addMidiClip (c.project, a, PPQ * 8, PPQ * 4);
    const Id a3 = edit::addMidiClip (c.project, a, PPQ * 32, PPQ * 4);   // past the range
    const Id b1 = edit::addMidiClip (c.project, b, PPQ * 4, PPQ * 4);
    const Id c1 = edit::addMidiClip (c.project, d, PPQ * 12, PPQ * 4);
    c.refresh();
    const ModifierKeys none, shift (ModifierKeys::shiftModifier), option (ModifierKeys::altModifier);

    c.chooseClip (a1, none);
    c.chooseClip (c1, shift);
    CHECK (c.selectedClips == (std::set<Id> { a1, a2, b1, c1 }));
    CHECK (! c.selectedClips.count (a3));
    c.chooseClip (a3, option);
    CHECK_EQ (c.selectedClips.size(), size_t (5));
    c.chooseClip (a2, ModifierKeys (ModifierKeys::shiftModifier | ModifierKeys::altModifier));
    CHECK (c.selectedClips == (std::set<Id> { a1, a3, b1, c1 }));
    c.chooseClip (b1, none);
    CHECK (c.selectedClips == (std::set<Id> { b1 }));
}

TEST ("zero crossings: with the option on, a cut lands on the very sample where the waveform crosses zero")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    // Two seconds of a 70 Hz sine: a crossing every 343 samples or so.
    const auto f = folder().getChildFile ("sine70.wav");
    {
        f.deleteFile();
        AudioBuffer<float> b (2, 96000);
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (0, i, 0.8f * (float) std::sin (2.0 * MathConstants<double>::pi * 70.0 * (i + 0.37) / 48000.0)), b.setSample (1, i, b.getSample (0, i));
        WavAudioFormat wav;
        std::unique_ptr<OutputStream> os (f.createOutputStream().release());
        auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Sine");
    const Id id = edit::addClip (c.project, t, audioClip (f, 0, c.project.tempo.tickAt (2.0)));
    c.refresh();
    for (int i = 0; i < 200 && ! engine.cache.forClip (*c.project.clip (id), c.project.tempo, c.projectFolder()); ++i) Thread::sleep (20);
    const auto audio = engine.cache.forClip (*c.project.clip (id), c.project.tempo, c.projectFolder());
    CHECK (audio != nullptr);
    if (! audio) return;
    auto valueAt = [&] (double seconds) { return audio->buffer.getSample (0, (int) std::llround (seconds * 48000)); };

    // Off: the cut is at the cursor, on its tick.
    c.cursor = c.project.tempo.tickAt (0.5013);
    auto e = c.zeroEdge (*c.project.clip (id), c.cursor);
    CHECK_EQ (e.tick, c.cursor);
    CHECK_EQ (e.fine, 0.0);

    // On: the nearest crossing, between ticks.
    c.zeroCrossings = true;
    e = c.zeroEdge (*c.project.clip (id), c.cursor);
    const double at = c.project.tempo.secondsAt (e.tick) + e.fine;
    CHECK (std::abs (valueAt (at)) < 0.01f);
    CHECK (std::abs (at - 0.5013) < 0.008);
    CHECK (std::abs (e.fine) <= 0.5 / (PPQ * 2.0) + 1e-9);   // under half a tick at 120 bpm

    // Split there: both halves meet on that sample.
    c.selectedClips = { id };
    c.splitAtCursor();
    const auto& clips = c.project.track (t)->clips();
    CHECK_EQ (clips.size(), size_t (2));
    if (clips.size() == 2)
    {
        CHECK_EQ (clips[0].end(), e.tick);
        CHECK (std::abs (clips[0].audio.endFine - e.fine) < 1e-12);
        CHECK (std::abs (clips[1].audio.startFine - e.fine) < 1e-12);
    }
}

TEST ("match loudness: a plug-in that turns the sound down 10 dB is brought back up, in the mix and the export")
{
    PluginManager plugins;
    Engine engine (plugins);
    // Eight seconds of noise.
    const auto f = folder().getChildFile ("noise8.wav");
    {
        f.deleteFile();
        AudioBuffer<float> b (2, 48000 * 8);
        Random r (11);
        for (int i = 0; i < b.getNumSamples(); ++i) { const float v = 0.2f * (r.nextFloat() - 0.5f); b.setSample (0, i, v); b.setSample (1, i, v); }
        WavAudioFormat wav;
        std::unique_ptr<OutputStream> os (f.createOutputStream().release());
        auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Noise");
    Clip c = audioClip (f, 0, p.tempo.tickAt (8.0));
    c.audio.sourceLength = 8.0;
    edit::addClip (p, t, c);
    p.track (t)->pan = 0;
    const Id fx = edit::addDevice (p, t, gain (-10), DeviceRole::AudioFx);

    Engine::RenderOptions o;
    o.start = 0; o.end = 8.0; o.tail = 0; o.dither = false; o.bitDepth = 32;
    String error;
    auto rmsOfLast = [] (const AudioBuffer<float>& b, double seconds)
    {
        const int n = (int) (seconds * 48000), from = b.getNumSamples() - n;
        return b.getRMSLevel (0, from, n);
    };
    const auto off = folder().getChildFile ("match-off.wav");
    CHECK (engine.render (p, folder(), off, o, {}, error));
    p.device (fx)->matchLoudness = true;
    const auto on = folder().getChildFile ("match-on.wav");
    CHECK (engine.render (p, folder(), on, o, {}, error));
    const float quiet = rmsOfLast (read (off), 2.0), matched = rmsOfLast (read (on), 2.0);
    // Down 10 dB without it; with it, back to the noise's own level (0.2 / sqrt 12 = 0.0577).
    CHECK (std::abs (Decibels::gainToDecibels (quiet / 0.0577f) + 10.0f) < 0.5f);
    CHECK (std::abs (Decibels::gainToDecibels (matched / 0.0577f)) < 1.0f);
    CHECK (std::abs (engine.matchingDb (fx) - 10.0f) < 1.0f);
}

#include "BuiltinInstruments.h"
#include "DeviceRack.h"
#include "Editors.h"
#include "PluginClip.h"

namespace
{
// A component on the screen, as Kalibre's window is: on the Mac a plug-in's window
// opens in the rack only then (DeviceRack::pluginsNeedAWindow, 0110).
struct OnScreen
{
    explicit OnScreen (Component& c) : comp (c) { comp.addToDesktop (0); comp.setVisible (true); }
    ~OnScreen() { comp.removeFromDesktop(); }
    Component& comp;
};
} // namespace

TEST ("plug-in windows: closed properly, so a plug-in opens its own window again; adding devices leaves the others' windows alone")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Synths");
    c.focusTrack = t;
    c.refresh();

    // Open, close, open: its own window each time, never a generic stand-in.
    {
        PluginRef r = synthRef();
        String error;
        auto inst = plugins.create (r, 48000, 512, error);
        CHECK (inst != nullptr);
        if (inst)
        {
            for (int i = 0; i < 3; ++i)
            {
                auto* e = editors::open (*inst);
                CHECK (dynamic_cast<GenericAudioProcessorEditor*> (e) == nullptr);
                CHECK (inst->getActiveEditor() == e);
                editors::close (e);
                CHECK (inst->getActiveEditor() == nullptr);
            }
        }
    }

    // In the rack: a third device added; the first two keep the very windows they had.
    DeviceRack rack (c);
    rack.setSize (1400, 360);
    OnScreen shown (rack);
    std::vector<Id> devices;
    c.apply ("Add", [&] (Project& p) { devices.push_back (edit::addDevice (p, t, synthRef(), DeviceRole::Instrument)); devices.push_back (edit::addDevice (p, t, samplerRef(), DeviceRole::Instrument)); });
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    // On the Mac a plug-in's window is held in a native view of its own (decision 0058), so it's asked for by device.
    auto editorsIn = [&] (DeviceRack& r)
    {
        std::vector<AudioProcessorEditor*> found;
        for (auto d : devices) if (auto* e = r.editorOf (d)) found.push_back (e);
        return found;
    };
    const auto before = editorsIn (rack);
    CHECK_EQ (before.size(), size_t (2));
    c.apply ("Add", [&] (Project& p) { devices.push_back (edit::addDevice (p, t, synthRef(), DeviceRole::Instrument)); });
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    const auto after = editorsIn (rack);
    CHECK_EQ (after.size(), size_t (3));
    for (auto* e : before) CHECK (std::find (after.begin(), after.end(), e) != after.end());
    for (auto* e : after) CHECK (dynamic_cast<GenericAudioProcessorEditor*> (e) == nullptr);
}

TEST ("musicxml: out of one song and, zipped as .mxl, into an empty one, with its tempo and key")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    c.project.tempo.setTempos ({ { 0, 84.0, false } });
    c.project.scale.root = 7;   // F
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Melody");
    const Id clip = edit::addMidiClip (c.project, t, 0, PPQ * 8);
    for (int i = 0; i < 6; ++i) { Note n; n.start = PPQ * i; n.length = PPQ / 2 * 3; n.pitch = 65 + i; edit::addNote (c.project, clip, n); }
    c.refresh();
    const auto xml = folder().getChildFile ("melody.musicxml");
    CHECK (c.exportMusicXml (xml));
    CHECK (xml.loadFileAsString().contains ("<part-name>Melody</part-name>"));

    // Zipped, as notation programs save it.
    const auto mxl = folder().getChildFile ("melody.mxl");
    mxl.deleteFile();
    {
        ZipFile::Builder zip;
        MemoryOutputStream container;
        container << "<?xml version=\"1.0\"?><container><rootfiles><rootfile full-path=\"score.musicxml\"/></rootfiles></container>";
        zip.addEntry (new MemoryInputStream (container.getMemoryBlock(), true), 9, "META-INF/container.xml", Time::getCurrentTime());
        zip.addEntry (new FileInputStream (xml), 9, "score.musicxml", Time::getCurrentTime());
        FileOutputStream out (mxl);
        CHECK (zip.writeToStream (out, nullptr));
    }
    c.newProject();
    const size_t before = c.project.tracks.size();
    CHECK (c.importMusicXml (mxl));
    CHECK_EQ (c.project.tracks.size(), before + 1);
    const auto& made = c.project.tracks.back();
    CHECK_EQ (made.name, std::string ("Melody"));
    CHECK (! made.clips().empty());
    if (! made.clips().empty()) CHECK_EQ (made.clips().front().midi.notes.size(), size_t (6));
    CHECK (std::abs (c.project.tempo.bpmAt (0) - 84.0) < 0.01);
    CHECK_EQ (c.project.scale.root, 7);
}

TEST ("questions: plug-in windows in the main window step aside while Kalibre asks something, and come back")
{
    // A plug-in's window on the Mac is a native view: drawn over anything
    // Kalibre draws, so it covered "Save the changes?" when quitting.
    PluginManager plugins;
    String error;
    auto inst = plugins.create (synthRef(), 48000, 512, error);
    CHECK (inst != nullptr);
    if (! inst) return;
    Component host;
    host.setSize (800, 600);
    ui::setHost (&host);
    auto* editor = editors::open (*inst);
    host.addAndMakeVisible (editor);

    auto* w = new AlertWindow ("Save the changes to this project?", {}, MessageBoxIconType::NoIcon);
    w->addButton ("Save", 1);
    int answered = -1;
    ui::present (w, ModalCallbackFunction::create ([&answered] (int r) { answered = r; }), true);
    CHECK (! editor->isVisible());
    w->exitModalState (1);
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK_EQ (answered, 1);
    CHECK (editor->isVisible());
    host.removeChildComponent (editor);
    editors::close (editor);
    ui::setHost (nullptr);
}

TEST ("plug-ins: a VST3 effect is an audio effect, though its description has no channel counts (BitFuzzer was filed as a MIDI effect)")
{
    // JUCE reads a VST3's moduleinfo.json for its description: no channel counts, 0 in and 0 out.
    PluginDescription d;
    d.pluginFormatName = "VST3";
    d.name = "BitFuzzer";
    d.category = "Fx|Distortion";
    d.numInputChannels = d.numOutputChannels = 0;
    CHECK (PluginManager::roleFor (d) == DeviceRole::AudioFx);
    d.category = "Fx|Instrument";
    d.isInstrument = true;
    CHECK (PluginManager::roleFor (d) == DeviceRole::Instrument);

    // A project saved with it as a MIDI effect, before its instrument: put back after it on opening.
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Bass");
    d = {};
    d.pluginFormatName = "VST3";
    d.name = "BitFuzzer";
    d.fileOrIdentifier = "/Library/Audio/Plug-Ins/VST3/BitFuzzer.vst3";
    d.category = "Fx|Distortion";
    const Id fx = edit::addDevice (p, t, PluginManager::refFor (d), DeviceRole::MidiFx);
    const Id synth = edit::addDevice (p, t, synthRef(), DeviceRole::Instrument);
    const auto f = folder().getChildFile ("refile.kalibre");
    CHECK (f.replaceWithText (String (file::save (p))));
    CHECK (c.open (f));
    const auto* track = c.project.track (t);
    CHECK (track != nullptr);
    if (! track) return;
    CHECK_EQ (track->devices.size(), size_t (2));
    if (track->devices.size() != 2) return;
    CHECK (track->devices[0].id == synth);
    CHECK (track->devices[1].id == fx);
    CHECK (track->devices[1].role == DeviceRole::AudioFx);
}

TEST ("plug-in panels: Kalibre's own keep their width and take the zone's height, never shrunk out of reading, + Add still beside them (0058 amended in the UAT)")
{
    // Others' plug-ins keep their shape, shown smaller to fit (0058); Kalibre's own lay
    // themselves out at any shape, so a short zone doesn't shrink their words to nothing.
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Synths");
    c.focusTrack = t;
    c.refresh();
    DeviceRack rack (c);
    rack.setSize (1400, 200);   // a short lower zone
    OnScreen onScreen (rack);
    Id sampler = 0;
    c.apply ("Add", [&] (Project& p) { sampler = edit::addDevice (p, t, samplerRef(), DeviceRole::Instrument); });
    MessageManager::getInstance()->runDispatchLoopUntil (50);

    auto* editor = rack.editorOf (sampler);
    CHECK (editor != nullptr);
    if (! editor) return;
    auto shownIn = [&]
    {
        rack.resized();
        const auto area = rack.pluginArea (sampler).toFloat();
       #if ! JUCE_MAC
        // Off the Mac it's an ordinary child: where JUCE draws it is where the rack says.
        const auto drawn = rack.getLocalArea (editor, editor->getLocalBounds().toFloat());
        CHECK (area.getPosition().getDistanceFrom (drawn.getPosition()) < 1.5f);
        CHECK (std::abs (area.getWidth() - drawn.getWidth()) < 1.5f && std::abs (area.getHeight() - drawn.getHeight()) < 1.5f);
       #endif
        return area;
    };
    auto shown = shownIn();
    CHECK (shown.getHeight() <= 200.0f - 30.0f);                          // under the header, inside the zone
    CHECK (rack.getLocalBounds().toFloat().contains (shown));
    CHECK (editor->getWidth() > 950);                                     // its own width, not shrunk to its shape
    CHECK (shown.getHeight() / (float) editor->getHeight() > 0.6f);       // drawn at least 60%: its words still read
    // + Add and Random beside it, in view.
    CHECK (rack.pluginArea (sampler).getRight() + 60 < rack.getWidth());

    // A tall zone: as tall as it is, not made bigger; as wide as the zone.
    rack.setSize (1400, 1400);
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    shown = shownIn();
    CHECK (std::abs (shown.getHeight() - (float) editor->getHeight()) < 1.5f);
    CHECK (editor->getHeight() <= 400);
    CHECK (shown.getWidth() > 950.0f);
}

TEST ("questions: plug-ins in the rack step aside too, though on the Mac they're held in native views of their own")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Synths");
    c.focusTrack = t;
    c.refresh();
    Component host;
    host.setSize (1400, 600);
    OnScreen shown (host);
    ui::setHost (&host);
    DeviceRack rack (c);
    host.addAndMakeVisible (rack);
    rack.setBounds (0, 200, 1400, 400);
    c.apply ("Add", [t] (Project& p) { edit::addDevice (p, t, synthRef(), DeviceRole::Instrument); });
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    std::vector<PluginClip*> clips;
    std::function<void (Component&)> walk = [&] (Component& comp)
    {
        if (auto* p = dynamic_cast<PluginClip*> (&comp)) clips.push_back (p);
        for (auto* child : comp.getChildren()) walk (*child);
    };
    walk (host);
    CHECK_EQ (clips.size(), size_t (1));
    if (clips.empty()) { ui::setHost (nullptr); return; }
    CHECK (clips[0]->isVisible());

    auto* w = new AlertWindow ("Save the changes to this project?", {}, MessageBoxIconType::NoIcon);
    w->addButton ("Save", 1);
    ui::present (w, ModalCallbackFunction::create ([] (int) {}), true);
    CHECK (! clips[0]->isVisible());
    w->exitModalState (1);
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK (clips[0]->isVisible());
    host.removeChildComponent (&rack);
    ui::setHost (nullptr);
}

TEST ("questions: a plug-in in a window of its own steps aside too, and the question is near the top (the musician's report: Save was covered)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Synths");
    c.focusTrack = t;
    c.refresh();
    Component host;
    host.setSize (1400, 900);
    Component top;
    host.addAndMakeVisible (top);
    top.setBounds (0, 0, 1400, 70);
    ui::setHost (&host);
    ui::setQuestionsBelow (&top);
    DeviceRack rack (c);
    host.addAndMakeVisible (rack);
    rack.setBounds (0, 500, 1400, 400);
    c.apply ("Add", [t] (Project& p) { edit::addDevice (p, t, synthRef(), DeviceRole::Instrument); });
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    const Id dev = c.project.track (t)->devices.front().id;
    rack.float_ (dev);
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    DocumentWindow* own = nullptr;
    for (int i = 0; i < Desktop::getInstance().getNumComponents(); ++i)
        if (auto* w = dynamic_cast<DocumentWindow*> (Desktop::getInstance().getComponent (i)))
            if (dynamic_cast<AudioProcessorEditor*> (w->getContentComponent()) != nullptr) own = w;
    CHECK (own != nullptr);
    if (! own) { ui::setHost (nullptr); ui::setQuestionsBelow (nullptr); return; }
    CHECK (own->isVisible());

    auto* w = new AlertWindow ("Save the changes to this project?", {}, MessageBoxIconType::NoIcon);
    w->addButton ("Save", 1);
    ui::present (w, ModalCallbackFunction::create ([] (int) {}), true);
    CHECK (! own->isVisible());
    CHECK_EQ (w->getY(), 82);   // just under the top bar
    CHECK (std::abs (w->getBounds().getCentreX() - 700) <= 1);
    w->exitModalState (1);
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK (own->isVisible());
    rack.dock (dev);
    host.removeChildComponent (&rack);
    ui::setHost (nullptr);
    ui::setQuestionsBelow (nullptr);
}

TEST ("engine: a plug-in taken out is deleted only once nothing else holds it and no window of it is open (Groove Agent SE crashed, deleted whenever JUCE's graph let go)")
{
    PluginManager plugins;
    Engine engine (plugins);
    FakeDevice device;
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Synths");
    const Id a = edit::addDevice (p, t, synthRef(), DeviceRole::Instrument);
    const Id b = edit::addDevice (p, t, synthRef(), DeviceRole::Instrument);
    startLive (engine, device, p);
    engine.play();
    playLive (engine, 0.05);
    const auto before = engine.waitingToGo();
    const auto later = [] { return Time::getMillisecondCounter() + 60000; };

    // Taken out while JUCE's graph still holds it (no block played since): kept, however long.
    edit::removeDevice (p, a);
    engine.sync (p, 0, folder());
    CHECK (engine.instance (a) == nullptr);
    CHECK_EQ (engine.waitingToGo(), before + 1);
    engine.letGoOfOld (later());
    CHECK_EQ (engine.waitingToGo(), before + 1);
    // A block played, and JUCE's graph's timer: now it goes.
    playLive (engine, 0.05);
    MessageManager::getInstance()->runDispatchLoopUntil (700);
    engine.letGoOfOld (later());
    CHECK_EQ (engine.waitingToGo(), before);

    // Taken out with its window open: kept until the window is closed.
    auto* inst = engine.instance (b);
    CHECK (inst != nullptr);
    if (! inst) return;
    editors::Owned window (editors::open (*inst));
    edit::removeDevice (p, b);
    engine.sync (p, 0, folder());
    playLive (engine, 0.05);
    MessageManager::getInstance()->runDispatchLoopUntil (700);
    engine.letGoOfOld (later());
    CHECK_EQ (engine.waitingToGo(), before + 1);
    window.reset();
    engine.letGoOfOld (later());
    CHECK_EQ (engine.waitingToGo(), before);
    engine.stop();
}

TEST ("rack: on the Mac a plug-in's window waits until its panel is in a window on the screen (HeartCore stopped Kalibre, given a view with no window)")
{
    const bool was = DeviceRack::pluginsNeedAWindow;
    DeviceRack::pluginsNeedAWindow = true;
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        const Id t = edit::addTrack (c.project, TrackKind::Track, "Synths");
        c.focusTrack = t;
        c.refresh();
        Component host;
        host.setSize (1400, 600);
        DeviceRack rack (c);
        host.addAndMakeVisible (rack);
        rack.setBounds (0, 200, 1400, 400);
        c.apply ("Add", [t] (Project& p) { edit::addDevice (p, t, synthRef(), DeviceRole::Instrument); });
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        const Id dev = c.project.track (t)->devices.front().id;
        auto* inst = engine.instance (dev);
        CHECK (inst != nullptr);
        if (inst)
        {
            // Not on the screen: no window asked of the plug-in yet.
            CHECK (inst->getActiveEditor() == nullptr);
            // On the screen: it opens.
            host.addToDesktop (0);
            host.setVisible (true);
            MessageManager::getInstance()->runDispatchLoopUntil (50);
            CHECK (inst->getActiveEditor() != nullptr);
            host.removeFromDesktop();
        }
        host.removeChildComponent (&rack);
    }
    DeviceRack::pluginsNeedAWindow = was;
}

#include "PianoRoll.h"

TEST ("auto cc: only on a track named for an instrument AutoCC knows; then the clip's CC lanes are drawn, as one undo step, and shown")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id piano = edit::addTrack (c.project, TrackKind::Track, "Piano");
    const Id violins = edit::addTrack (c.project, TrackKind::Track, "Violins 1");
    const Id pc = edit::addMidiClip (c.project, piano, 0, PPQ * 16);
    const Id vc = edit::addMidiClip (c.project, violins, 0, PPQ * 16);
    for (Id clip : { pc, vc }) { Note n; n.start = PPQ; n.length = PPQ * 6; n.pitch = 67; edit::addNote (c.project, clip, n); }
    c.refresh();

    c.selectClip (pc);
    CHECK (c.autoCcFor().family == autocc::Family::None);
    c.autoCc();
    CHECK (c.project.clip (pc)->midi.events.empty());
    CHECK (c.message.contains ("Violins"));          // says what it needs

    c.selectClip (vc);
    CHECK (c.autoCcFor().family == autocc::Family::Strings);
    c.autoCc();
    const auto& evs = c.project.clip (vc)->midi.events;
    std::set<int> ccs;
    for (auto& e : evs) ccs.insert (e.number);
    CHECK (ccs == std::set<int> ({ 1, 7, 11, 21 }));
    c.undo();
    CHECK (c.project.clip (vc)->midi.events.empty());
    c.redo();
    CHECK (! c.project.clip (vc)->midi.events.empty());

    // In the piano roll: a lane for each, under the notes.
    PianoRoll roll (c);
    roll.setSize (1000, 700);
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    CcLanes* lanes = nullptr;
    for (auto* child : roll.getChildren()) if (auto* l = dynamic_cast<CcLanes*> (child)) lanes = l;
    CHECK (lanes != nullptr);
    if (! lanes) return;
    CHECK (lanes->shown() == std::vector<int> ({ 1, 11, 7, 21 }));
    CHECK (lanes->getHeight() > 0 && lanes->getBottom() == roll.getHeight());
}

#include "PatternEditors.h"

TEST ("random: each pattern editor's Random writes a new pattern of its own each click, as one undo step, leaving the piano roll's notes")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Beat");
    const Id clip = edit::addMidiClip (c.project, t, 0, PPQ * 8);
    Note played; played.start = 0; played.length = PPQ * 8; played.pitch = 48;
    edit::addNote (c.project, clip, played);
    c.refresh();
    c.selectClip (clip);
    c.grid = PPQ / 4;

    auto press = [] (Component& editor)
    {
        for (auto* child : editor.getChildren())
            if (auto* b = dynamic_cast<TextButton*> (child); b && b->getButtonText() == "Random") { b->triggerClick(); break; }
        MessageManager::getInstance()->runDispatchLoopUntil (20);
    };
    auto mine = [&] (NoteFrom who)
    {
        std::vector<std::pair<Tick, int>> out;
        for (auto& n : c.project.clip (clip)->midi.notes) if (n.from == who) out.push_back ({ n.start, n.pitch });
        return out;
    };
    auto rollKept = [&] { return std::any_of (c.project.clip (clip)->midi.notes.begin(), c.project.clip (clip)->midi.notes.end(),
                                              [] (const Note& n) { return n.from == NoteFrom::Roll && n.pitch == 48; }); };

    DrumGrid drums (c);
    drums.setSize (900, 400);
    press (drums);
    const auto first = mine (NoteFrom::Drums);
    CHECK (! first.empty());
    CHECK (rollKept());
    CHECK (c.message.contains ("Good Idea"));
    press (drums);
    CHECK (mine (NoteFrom::Drums) != first);               // a new one each click
    c.undo();
    CHECK (mine (NoteFrom::Drums) == first);               // one undo step each

    StepSequencer steps (c);
    steps.setSize (900, 400);
    press (steps);
    const auto tune = mine (NoteFrom::Steps);
    CHECK (! tune.empty());
    for (auto& [at, pitch] : tune) CHECK (at % (PPQ / 4) == 0);   // one a step, on the steps
    CHECK (rollKept());
    CHECK (! mine (NoteFrom::Drums).empty());               // the drums are still there

    EuclidEditor euclid (c);
    euclid.setSize (900, 400);
    press (euclid);
    const auto rhythm = mine (NoteFrom::Euclid);
    CHECK (! rhythm.empty());
    CHECK (rollKept() && ! mine (NoteFrom::Steps).empty());
}

TEST ("swing and gate: a drag of the drum grid's or step sequencer's slider moves their notes, is saved with the clip, and is one undo step")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Beat");
    const Id clip = edit::addMidiClip (c.project, t, 0, PPQ * 4);
    c.grid = PPQ / 4;
    {
        patterns::Grid g;
        g.stepTicks = PPQ / 4;
        g.steps = 16;
        g.lanes = { { "Closed Hat", 42, std::vector<int> (16, 100) } };
        unsigned next = 1000;
        patterns::writeGrid (c.project.clip (clip)->midi, g, 0, PPQ * 4, next, NoteFrom::Drums);
    }
    c.refresh();
    c.selectClip (clip);

    auto drag = [] (Component& editor, int which, double to)
    {
        for (auto* child : editor.getChildren())
            if (auto* f = dynamic_cast<FeelControls*> (child))
            {
                f->began();
                int k = 0;
                for (auto* s : f->getChildren())
                    if (auto* slider = dynamic_cast<Slider*> (s); slider && k++ == which) slider->setValue (to, sendNotificationSync);   // as a mouse drag does
                f->ended();
            }
        MessageManager::getInstance()->runDispatchLoopUntil (20);
    };
    auto hatAt = [&] (int step)
    {
        for (auto& n : c.project.clip (clip)->midi.notes) if (n.start / (PPQ / 4) == step && n.from == NoteFrom::Drums) return n;
        return Note {};
    };

    DrumGrid drums (c);
    drums.setSize (1000, 400);
    drag (drums, 0, 50);                                   // swing 50%
    CHECK_EQ (hatAt (1).start, PPQ / 4 + PPQ / 8);           // the off-step half a step late
    CHECK_EQ (hatAt (2).start, PPQ / 2);                     // the on-step where it was
    CHECK_EQ (c.project.clip (clip)->drumsFeel.swing, 0.5);
    drag (drums, 1, 25);                                   // gate 25%
    CHECK_EQ (hatAt (2).length, PPQ / 16);
    c.undo();                                              // the gate's drag, alone
    CHECK_EQ (c.project.clip (clip)->drumsFeel.gate, 0.9);
    CHECK_EQ (hatAt (1).start, PPQ / 4 + PPQ / 8);
    c.undo();
    CHECK_EQ (hatAt (1).start, PPQ / 4);

    StepSequencer steps (c);
    steps.setSize (1000, 400);
    {
        // Two steps on.
        auto* cl = c.project.clip (clip);
        patterns::StepSequence s;
        s.steps.assign (16, {});
        s.steps[0].on = s.steps[3].on = true;
        unsigned next = 2000;
        patterns::writeSteps (cl->midi, s, 0, PPQ * 4, next, NoteFrom::Steps);
        c.refresh();
    }
    drag (steps, 1, 25);                                   // gate 25% for every step
    for (auto& n : c.project.clip (clip)->midi.notes)
        if (n.from == NoteFrom::Steps) CHECK_EQ (n.length, PPQ / 16);
    CHECK_EQ (c.project.clip (clip)->stepsFeel.gate, 0.25);
}

TEST ("arming: click for audio (red), right-click for MIDI (blue), Shift-click for both, Shift-right-click for MIDI input; the same again disarms")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Keys");
    c.refresh();
    auto track = [&] { return c.project.track (t); };
    c.arm (t, ArmFor::Audio);
    CHECK (track()->armedAudio() && ! track()->armedPlugins() && ! track()->armedMidiIn());
    c.arm (t, ArmFor::Midi);                                 // another kind: switched to it
    CHECK (! track()->armedAudio() && track()->armedPlugins());
    c.arm (t, ArmFor::Midi);                                 // the same again: off
    CHECK (! track()->armed);
    c.arm (t, ArmFor::Both);
    CHECK (track()->armedAudio() && track()->armedPlugins() && ! track()->armedMidiIn());
    c.arm (t, ArmFor::MidiInput);
    CHECK (! track()->armedAudio() && ! track()->armedPlugins() && track()->armedMidiIn());
    c.arm (t, ArmFor::MidiInput);
    CHECK (! track()->armed);
    // A Summing track records sound only.
    const Id sum = edit::addTrack (c.project, TrackKind::Summing, "Summing");
    c.refresh();
    for (auto how : { ArmFor::Midi, ArmFor::Both, ArmFor::MidiInput })
    {
        c.arm (sum, how);
        CHECK (c.project.track (sum)->armed && c.project.track (sum)->armFor == ArmFor::Audio);
        c.arm (sum, ArmFor::Audio);
    }
    // Saved.
    c.arm (t, ArmFor::Both);
    auto loaded = file::load (file::save (c.project));
    CHECK (loaded && loaded->track (t)->armed && loaded->track (t)->armFor == ArmFor::Both);
}

TEST ("arming for MIDI: what a free-running arpeggiator plays into the instrument is recorded as a MIDI clip; armed for audio, it isn't")
{
    PluginManager plugins;
    Engine engine (plugins);
    FakeDevice device;
    Controller c (engine);
    c.project = Project();
    c.project.tempo.setTempos ({ { 0, 120.0, false } });
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Arp");
    edit::addDevice (c.project, t, PluginRef { "Kalibre", "test-arp", "Arp" }, DeviceRole::MidiFx);
    edit::addDevice (c.project, t, synthRef(), DeviceRole::Instrument);
    c.focusTrack = t;
    c.refresh();
    startLive (engine, device, c.project);
    c.arm (t, ArmFor::Midi);
    c.refresh();

    c.cursor = 0;
    c.record();
    playLive (engine, 2.0);
    c.stop();
    const auto* track = c.project.track (t);
    CHECK_EQ (track->clips().size(), size_t (1));
    if (track->clips().empty()) return;
    const auto& clip = track->clips().front();
    CHECK (clip.isMidi());
    // A note every quarter of a second (the arpeggiator's, not anything played in): about eight in two seconds.
    CHECK (clip.midi.notes.size() >= 6 && clip.midi.notes.size() <= 9);
    for (auto& n : clip.midi.notes) CHECK_EQ (n.pitch, 72);
    if (clip.midi.notes.size() >= 2)
    {
        const double gap = c.project.tempo.secondsAt (clip.start + clip.midi.notes[1].start) - c.project.tempo.secondsAt (clip.start + clip.midi.notes[0].start);
        CHECK (std::abs (gap - 0.25) < 0.01);
    }

    // Armed for audio instead: its sound coming in is recorded, but no MIDI from it.
    c.arm (t, ArmFor::Audio);
    c.refresh();
    c.cursor = 0;
    c.record();
    playLive (engine, 1.0);
    c.stop();
    const auto& after = c.project.track (t)->clips();
    CHECK_EQ (std::count_if (after.begin(), after.end(), [] (const Clip& x) { return x.isMidi(); }), 1);
}

TEST ("arming for both records the sound coming in and the instrument's MIDI; for MIDI input, the keys played, not the arpeggiator's notes")
{
    PluginManager plugins;
    Engine engine (plugins);
    FakeDevice device;
    Controller c (engine);
    c.project = Project();
    c.project.tempo.setTempos ({ { 0, 120.0, false } });
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Arp");
    c.project.track (t)->input.audioChannel = 0;
    edit::addDevice (c.project, t, PluginRef { "Kalibre", "test-arp", "Arp" }, DeviceRole::MidiFx);
    edit::addDevice (c.project, t, synthRef(), DeviceRole::Instrument);
    c.focusTrack = t;
    c.refresh();
    startLive (engine, device, c.project);
    auto count = [&] (bool midi)
    {
        const auto& clips = c.project.track (t)->clips();
        return (int) std::count_if (clips.begin(), clips.end(), [midi] (const Clip& x) { return x.isMidi() == midi; });
    };

    // Both: an audio clip and a MIDI clip of the arpeggiator's notes.
    c.arm (t, ArmFor::Both);
    c.refresh();
    c.cursor = 0;
    c.record();
    playLive (engine, 1.0);
    c.stop();
    CHECK_EQ (count (false), 1);
    CHECK_EQ (count (true), 1);

    // MIDI input: a key played (E4), not the arpeggiator's C5s; no sound recorded.
    c.arm (t, ArmFor::MidiInput);
    c.refresh();
    c.cursor = c.project.tempo.tickAt (4.0);
    c.record();
    playLive (engine, 0.25);
    engine.playIn (MidiMessage::noteOn (1, 64, (uint8) 90));
    playLive (engine, 0.5);
    engine.playIn (MidiMessage::noteOff (1, 64));
    playLive (engine, 0.25);
    c.stop();
    CHECK_EQ (count (false), 1);
    CHECK_EQ (count (true), 2);
    for (auto& clip : c.project.track (t)->clips())
        if (clip.isMidi() && clip.start >= c.project.tempo.tickAt (3.9))
        {
            CHECK_EQ (clip.midi.notes.size(), size_t (1));
            if (! clip.midi.notes.empty()) CHECK_EQ (clip.midi.notes.front().pitch, 64);
        }
}

#include "NotationView.h"

TEST ("notation: the playhead moves across the staff while playing, round a looped clip's repeats")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Flute");
    const Id clip = edit::addMidiClip (c.project, t, 0, PPQ * 4);
    for (int i = 0; i < 4; ++i) { Note n; n.start = PPQ * i; n.length = PPQ; n.pitch = 72; edit::addNote (c.project, clip, n); }
    c.refresh();
    c.selectClip (clip);
    NotationView view (c);
    view.setSize (1000, 400);
    bool on = false;
    auto drawnAt = [&] (double beats)
    {
        // No audio device here: moved while stopped, then playing again.
        engine.stop();
        engine.locate (c.project.tempo.secondsAt ((Tick) (beats * PPQ)));
        if (on) engine.play();
        view.createComponentSnapshot (view.getLocalBounds());
        return view.shownPlayheadX;
    };
    CHECK (drawnAt (1.0) < 0);                 // stopped: none
    on = true;
    const float a = drawnAt (1.0), b = drawnAt (2.5);
    CHECK (a > 0);
    CHECK (b > a);                             // it moves on
    c.apply ("Loop Clip", [clip] (Project& p) { edit::loopContent (p, clip); });
    const float again = drawnAt (5.0);         // the second pass, beat 1 again
    CHECK (std::abs (again - a) < 1.0f);
    engine.stop();
}

#include "TopBar.h"

TEST ("message line: a message gives way to what's chosen, or what the pointer is over; notes say pitch, velocity and length; clips their length and notes (0070)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Keys");
    const Id clip = edit::addMidiClip (c.project, t, PPQ * 4 * 48, PPQ * 4 * 4);
    Note n; n.start = 0; n.length = PPQ / 2; n.pitch = 60; n.velocity = 100;
    const Id note = edit::addNote (c.project, clip, n);
    n.start = PPQ; n.pitch = 64;
    edit::addNote (c.project, clip, n);
    c.refresh();
    c.selectedClips.clear();
    c.selectedNotes.clear();

    TopBar bar (c);
    bar.setSize (1600, bar.preferredHeight());
    c.say ("Deleted the track.");
    c.retireMessage();
    CHECK_EQ (c.message, String ("Deleted the track."));
    // Nothing chosen: pointing at a clip says its length and notes, and the message gives way.
    c.hover (clip);
    c.retireMessage();
    CHECK (c.message.isEmpty());
    const auto clipInfo = c.infoLine();
    CHECK (clipInfo.contains ("4 bars"));
    CHECK (clipInfo.contains ("2 notes"));
    CHECK (clipInfo.contains ("49.1.000"));
    // A chosen note: its pitch, velocity and length, whatever the pointer is over.
    c.selectedClip = clip;
    c.selectedNotes = { note };
    const auto noteInfo = c.infoLine();
    CHECK (noteInfo.contains ("C4"));
    CHECK (noteInfo.contains ("Velocity 100"));
    CHECK (noteInfo.contains ("Length 1/8"));
    // A warning stays while only the pointer moves; a new choice lets it go.
    c.say ("Something went wrong.", true);
    c.hover (0);
    c.hover (clip);
    c.retireMessage();
    CHECK (c.message.isNotEmpty());
    c.selectedNotes.clear();
    c.retireMessage();
    CHECK (c.message.isEmpty());
}

TEST ("top bar: right-click hides what isn't wanted; the Grid is the arrangement's; Shift-click Follow turns pages (0070)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    TopBar bar (c);
    bar.setSize (1800, 200);
    TransportBar transport (c);   // Loop, Punch, Click and Count-in are along the bottom (0095)
    transport.setSize (1800, 44);
    auto shownNamed = [&] (const String& text)
    {
        for (auto* parent : { (Component*) &bar, (Component*) &transport })
            for (auto* ch : parent->getChildren())
                if (auto* b = dynamic_cast<Button*> (ch); b && b->getButtonText() == text) return b->isVisible();
        return false;
    };
    CHECK (shownNamed ("Tap") && shownNamed ("Punch") && shownNamed ("Count-in"));
    c.hiddenInTopBar = { "Tap", "Punch", "Count-in" };
    bar.resized();
    transport.resized();
    CHECK (! shownNamed ("Tap") && ! shownNamed ("Punch") && ! shownNamed ("Count-in"));
    CHECK (shownNamed ("Loop"));
    CHECK (TopBar::hideable().contains ("Grid"));

    // The arrangement snaps to its own grid; the editors keep theirs.
    c.grid = PPQ / 4;
    c.arrangeGrid = PPQ * 4;
    CHECK_EQ (c.arrangeSnap (PPQ * 3), Tick (PPQ * 4));
    CHECK_EQ (c.snapTick (PPQ * 3), Tick (PPQ * 3));

    // Follow: a click scrolls, a Shift-click turns pages, and each clicked again stops following.
    Button* follow = nullptr;
    for (auto* ch : bar.getChildren())
        if (auto* b = dynamic_cast<Button*> (ch); b && b->getButtonText().startsWith ("Follow")) follow = b;
    CHECK (follow != nullptr);
    if (! follow) return;
    c.followPlayback = false;
    follow->triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    CHECK (c.followPlayback && ! c.followPages);
    c.followPages = true;      // as a Shift-click leaves it
    bar.resized();
    // The bar's timer shows it; a busy machine (the Mac on CI) can take longer than a few ticks.
    for (int i = 0; i < 50 && follow->getButtonText() != "Follow: Page"; ++i) MessageManager::getInstance()->runDispatchLoopUntil (20);
    CHECK (follow->getButtonText() == "Follow: Page");
}

#include "ArticulationManagerView.h"

TEST ("articulation manager: maps imported into the library, used on a track from the piano roll's menu, one undo step (0072)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    c.articulationFolder = folder().getChildFile ("articulation-library");
    c.articulationFolder.deleteRecursively();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Violins");
    c.focusTrack = t;
    c.refresh();

    const File fixtures = File (KALIBRE_SOURCE_DIR).getChildFile ("Tests/fixtures/articulations");
    Array<File> files { fixtures.getChildFile ("Test Strings.expressionmap"), fixtures.getChildFile ("Test Strings.reabank"), fixtures.getChildFile ("Test Strings.plist") };
    CHECK_EQ (c.importArticulationMaps (files), 4);
    CHECK (c.message.contains ("Imported"));
    const auto library = c.articulationLibrary();
    CHECK_EQ (library.size(), size_t (4));
    // Imported again: replaced, not doubled.
    c.importArticulationMaps ({ fixtures.getChildFile ("Test Strings.plist") });
    CHECK_EQ (c.articulationLibrary().size(), size_t (4));

    ArticulationManagerView view (c);
    view.setSize (900, 500);
    CHECK_EQ (view.shownRows().size(), size_t (4));

    // From the piano roll's Velocity menu: the library's maps are there.
    PianoRoll roll (c);
    roll.setSize (900, 500);
    auto menu = roll.articulationMenu();
    bool listed = false;
    for (PopupMenu::MenuItemIterator it (menu); it.next();) listed |= it.getItem().text == "Violins";
    CHECK (listed);

    const auto before = c.history.size();
    ArticulationMap violins;
    for (auto& m : library) if (m.name == "Violins") violins = m;
    c.useArticulationMap (t, violins);
    CHECK_EQ (c.history.size(), before + 1);
    const auto* map = c.project.articulationMap (c.project.track (t)->articulationMap);
    CHECK (map != nullptr);
    if (map) CHECK_EQ (map->name, std::string ("Violins"));
    // Used again: the song's copy, not a second one.
    c.useArticulationMap (t, violins);
    CHECK_EQ (c.project.articulationMaps.size(), size_t (1));
    view.refresh();
    bool both = false;
    for (auto& r : view.shownRows()) both |= r.map.name == "Violins" && r.inLibrary && r.inSong;
    CHECK (both);

    // Each imported articulation's own delay (0082): set in the manager, kept in the
    // library for the next song and in this song's copy, one undo step.
    const auto steps = c.history.size();
    CHECK (c.setArticulationDelay ("Violins", { 0, 2 }, -80));
    CHECK_EQ (c.history.size(), steps + 1);
    for (auto& m : c.articulationLibrary())
        if (m.name == "Violins")
        {
            CHECK_EQ (m.articulations[0].delayMs, -80.0);
            CHECK_EQ (m.articulations[1].delayMs, 0.0);
            CHECK_EQ (m.articulations[2].delayMs, -80.0);
        }
    if (const auto* song = c.project.articulationMap (c.project.track (t)->articulationMap))
        CHECK_EQ (song->articulations[0].delayMs, -80.0);
    // The manager shows it, and sets it from its own field.
    view.refresh();
    for (int i = 0; i < (int) view.shownRows().size(); ++i) if (view.shownRows()[(size_t) i].map.name == "Violins") view.choose (i);
    view.chooseArticulation (1);
    view.typeDelay ("25");
    for (auto& m : c.articulationLibrary()) if (m.name == "Violins") CHECK_EQ (m.articulations[1].delayMs, 25.0);
    CHECK (c.undo());
    if (const auto* song = c.project.articulationMap (c.project.track (t)->articulationMap))
        CHECK_EQ (song->articulations[1].delayMs, 0.0);
}

#include "CrashReports.h"
#include "CrashContext.h"
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>

TEST ("crash reports: a crash writes what went wrong and the plug-in that was playing, then ends as before (0073)")
{
    const auto reports = folder().getChildFile ("crash-reports");
    reports.deleteRecursively();
    reports.createDirectory();
    const auto before = Time::getCurrentTime() - RelativeTime::seconds (2);
    const pid_t child = fork();
    if (child == 0)
    {
        crash::install (reports, "test");
        crashcontext::audioPlugin = "Riffer";
        std::raise (SIGSEGV);
        _exit (0);   // never reached
    }
    int status = 0;
    waitpid (child, &status, 0);
    CHECK (WIFSIGNALED (status) && WTERMSIG (status) == SIGSEGV);   // it still ends as a crash
    const auto found = crash::since (reports, before);
    CHECK_EQ (found.size(), 1);
    if (found.isEmpty()) return;
    CHECK_EQ (crash::summary (found.getFirst()), String ("a bad memory access, in Riffer"));
    CHECK (found.getFirst().loadFileAsString().contains ("Where, the newest call first"));
    CHECK (crash::since (reports, Time::getCurrentTime() + RelativeTime::seconds (5)).isEmpty());
}

TEST ("backups: one while it has changed, none when it hasn't; the newest 48 kept; opened as the song, unsaved (0073)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    c.backupsRoot = folder().getChildFile ("backups");
    c.backupsRoot.deleteRecursively();
    edit::addTrack (c.project, TrackKind::Track, "Piano");
    c.refresh();
    CHECK (c.backup());
    CHECK (! c.backup());                     // nothing changed since
    CHECK_EQ (c.backups().size(), 1);
    for (int i = 0; i < 50; ++i)
    {
        edit::addTrack (c.project, TrackKind::Track, "More " + std::to_string (i));
        CHECK (c.backup());
    }
    CHECK_EQ (c.backups().size(), Controller::backupsKept);
    const auto newest = c.backups().getFirst();
    CHECK (newest.loadFileAsString().contains ("More 49"));

    // A saved song's backups go beside it, in Backups.
    const auto song = folder().getChildFile ("backup-song").getChildFile ("Song.kalibre");
    song.getParentDirectory().deleteRecursively();
    song.getParentDirectory().createDirectory();
    CHECK (song.replaceWithText (String (file::save (c.project))));
    c.file = song;
    edit::addTrack (c.project, TrackKind::Track, "Strings");
    CHECK (c.backup());
    CHECK (song.getParentDirectory().getChildFile ("Backups").isDirectory());
    const auto b = c.backups().getFirst();
    edit::addTrack (c.project, TrackKind::Track, "Later");
    CHECK (c.openBackup (b));
    CHECK (c.file == song);                   // saved over the song, not the backup
    CHECK (c.dirty);
    bool later = false;
    for (auto& t : c.project.tracks) later |= t.name == "Later";
    CHECK (! later);
}

#include "Arrange.h"

TEST ("track headers: an I/O button beside M, S and R says where the track listens and plays to (0074)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Keys");
    const Id ret = edit::addTrack (c.project, TrackKind::Return, "Reverb");
    c.project.track (t)->output = ret;
    c.refresh();
    Arrange arrange (c);
    arrange.setSize (1200, 600);
    c.refresh();
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    std::function<Button* (Component&)> find = [&] (Component& p) -> Button*
    {
        for (auto* ch : p.getChildren())
        {
            if (auto* b = dynamic_cast<Button*> (ch); b && b->getButtonText() == "I/O" && b->isVisible()
                && b->getTooltip().contains ("Reverb")) return b;
            if (auto* f = find (*ch)) return f;
        }
        return nullptr;
    };
    auto* io = find (arrange);
    CHECK (io != nullptr);
    if (io) CHECK (io->getTooltip().contains ("every keyboard"));
}

TEST ("select and explode: the Select menu's choices through the controller; Explode Chords one undo step (0075)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Piano");
    const Id clip = edit::addMidiClip (c.project, t, 0, PPQ * 4);
    for (int pitch : { 48, 52, 55 }) { Note n; n.start = 0; n.length = PPQ * 4; n.pitch = pitch; edit::addNote (c.project, clip, n); }
    c.refresh();
    c.selectClip (clip);
    c.selectNotes (noteselect::What::Third);
    CHECK_EQ (c.selectedNotes.size(), size_t (1));
    CHECK (c.message.contains ("thirds"));
    c.selectNotes (noteselect::What::Seventh);
    CHECK (c.selectedNotes.empty());
    const auto tracksBefore = c.project.tracks.size();
    const auto steps = c.history.size();
    c.explodeChords ({ clip });
    CHECK_EQ (c.project.tracks.size(), tracksBefore + 3);
    CHECK_EQ (c.history.size(), steps + 1);
    c.undo();
    CHECK_EQ (c.project.tracks.size(), tracksBefore);
    CHECK (c.project.clip (clip) != nullptr);
}

TEST ("step input: keys from a MIDI keyboard, a number for the length, written at the cursor on letting go, the cursor moving on (0076)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Piano");
    const Id clip = edit::addMidiClip (c.project, t, 0, PPQ * 4);
    c.focusTrack = t;
    c.refresh();
    c.selectClip (clip);
    c.locate (PPQ);
    c.setStepInput (true);
    CHECK (c.message.contains ("1/16"));
    CHECK (c.stepDigit (9));
    CHECK (c.stepDigit (5));                       // a dotted 1/8
    const auto steps = c.history.size();
    engine.playIn (MidiMessage::noteOn (1, 60, (uint8) 100));
    engine.playIn (MidiMessage::noteOn (1, 64, (uint8) 90));
    engine.playIn (MidiMessage::noteOff (1, 60));
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK_EQ (c.project.clip (clip)->midi.notes.size(), size_t (0));   // one key still held
    engine.playIn (MidiMessage::noteOff (1, 64));
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    const auto& notes = c.project.clip (clip)->midi.notes;
    CHECK_EQ (notes.size(), size_t (2));
    for (auto& n : notes) { CHECK_EQ (n.start, Tick (PPQ)); CHECK_EQ (n.length, Tick (PPQ * 3 / 4)); }
    CHECK_EQ (c.cursor, Tick (PPQ + PPQ * 3 / 4));
    CHECK_EQ (c.history.size(), steps + 1);
    // Past the clip's end it grows; in the drum grid the notes are the drums'.
    c.editor = EditorKind::Drums;
    c.stepDigit (8);
    engine.playIn (MidiMessage::noteOn (10, 36, (uint8) 110));
    engine.playIn (MidiMessage::noteOff (10, 36));
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    const auto* cl = c.project.clip (clip);
    CHECK_EQ (cl->length, Tick (PPQ * 8));
    bool drum = false;
    for (auto& n : cl->midi.notes) drum |= n.pitch == 36 && n.from == NoteFrom::Drums && n.length == PPQ * 4;
    CHECK (drum);
    // A rest: the - key, as long as the length chosen; the cursor moves on, nothing written,
    // and the clip grows to hold it.
    const auto notesBefore = c.project.clip (clip)->midi.notes.size();
    c.locate (PPQ * 8 - PPQ);
    c.stepDigit (6);
    CHECK (c.stepRest());
    CHECK_EQ (c.cursor, Tick (PPQ * 8));
    c.stepDigit (9);
    CHECK (c.stepRest());                            // a dotted 1/4
    CHECK_EQ (c.cursor, Tick (PPQ * 8 + PPQ * 3 / 2));
    CHECK_EQ (c.project.clip (clip)->length, Tick (PPQ * 12));
    engine.playIn (MidiMessage::noteOn (1, 72, (uint8) 100));
    engine.playIn (MidiMessage::noteOff (1, 72));
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK_EQ (c.project.clip (clip)->midi.notes.size(), notesBefore + 1);
    bool after = false;
    for (auto& n : c.project.clip (clip)->midi.notes) after |= n.pitch == 72 && n.start == PPQ * 8 + PPQ * 3 / 2 && n.length == PPQ;
    CHECK (after);
    // Off: keys are only heard.
    c.setStepInput (false);
    engine.playIn (MidiMessage::noteOn (1, 70, (uint8) 100));
    engine.playIn (MidiMessage::noteOff (1, 70));
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK_EQ (c.project.clip (clip)->midi.notes.size(), notesBefore + 1);
    CHECK (! c.stepDigit (4));
    CHECK (! c.stepRest());
}

TEST ("dawproject: a song exported with its audio and Kalibre's own devices, and opened again as a new song (0077)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Drums");
    edit::addClip (c.project, t, audioClip (clickFile (0.5), 0, PPQ * 4));
    const Id synthTrack = edit::addTrack (c.project, TrackKind::Track, "Keys");
    edit::addDevice (c.project, synthTrack, synthRef(), DeviceRole::Instrument);
    const Id clip = edit::addMidiClip (c.project, synthTrack, 0, PPQ * 4);
    Note n; n.pitch = 64; n.length = PPQ; edit::addNote (c.project, clip, n);
    c.refresh();
    c.storeDeviceStates();
    const auto stateBefore = c.project.track (synthTrack)->devices.front().state;
    CHECK (! stateBefore.empty());

    const auto f = folder().getChildFile ("song.dawproject");
    f.deleteFile();
    folder().getChildFile ("song Audio").deleteRecursively();
    CHECK (c.exportDawProject (f));
    CHECK (f.existsAsFile());
    {
        ZipFile zip (f);
        CHECK (zip.getEntry ("project.xml") != nullptr);
        CHECK (zip.getEntry ("metadata.xml") != nullptr);
        bool audio = false;
        for (int i = 0; i < zip.getNumEntries(); ++i) audio |= zip.getEntry (i)->filename.startsWith ("audio/");
        CHECK (audio);
    }
    c.project = Project();
    CHECK (c.importDawProject (f));
    CHECK_EQ (c.project.tracks.size(), size_t (2));
    if (c.project.tracks.size() != 2) return;
    const auto& drums = c.project.tracks[0];
    CHECK_EQ (drums.clips().size(), size_t (1));
    if (! drums.clips().empty()) CHECK (File (String (drums.clips()[0].audio.file)).existsAsFile());
    const auto& keys = c.project.tracks[1];
    CHECK_EQ (keys.devices.size(), size_t (1));
    if (! keys.devices.empty()) CHECK_EQ (keys.devices[0].state, stateBefore);
    CHECK_EQ (keys.clips().size(), size_t (1));
    CHECK (c.file == File());
    CHECK (c.dirty);
}

TEST ("multi-core: playing live through an audio device, the plan plays the blocks once JUCE's graph has had its one, and the sound is there (0078)")
{
    PluginManager plugins;
    Engine engine (plugins);
    engine.setParallel (true);
    FakeDevice device;
    Project p;
    for (int i = 0; i < 3; ++i)
    {
        const Id t = edit::addTrack (p, TrackKind::Track, "Click " + std::to_string (i));
        edit::addClip (p, t, audioClip (clickFile (0.25), 0, PPQ * 4));
        edit::addDevice (p, t, gain (-6.0f * (float) i), DeviceRole::AudioFx);
        p.track (t)->pan = 0;
    }
    startLive (engine, device, p);
    engine.locate (0);
    engine.play();
    const auto live = playLive (engine, 0.5);
    engine.stop();
    CHECK (engine.audioGraph().blocksByPlan.load() > 30);
    CHECK (peak (live, 0).second > 0.5f);
}

#include "NoteSplitter.h"
#include "NoteSplitterEditor.h"

TEST ("note splitter: a chord's notes, top to bottom, into four tracks' MIDI inputs, recorded there; the chord still plays on its own track (0083)")
{
    PluginManager plugins;
    Engine engine (plugins);
    FakeDevice device;
    Controller c (engine);
    c.project = Project();
    const Id chords = edit::addTrack (c.project, TrackKind::Track, "Chords");
    const Id clip = edit::addMidiClip (c.project, chords, 0, PPQ * 4);
    for (int pitch : { 48, 55, 64, 60 }) { Note n; n.start = PPQ / 2; n.length = PPQ; n.pitch = pitch; edit::addNote (c.project, clip, n); }
    const Id dev = edit::addDevice (c.project, chords, noteSplitterRef(), DeviceRole::MidiFx);
    CHECK_EQ (c.project.device (dev)->midiTo.size(), size_t (4));
    std::vector<Id> voices;
    for (const char* name : { "Violins I", "Violins II", "Violas", "Cellos" })
    {
        const Id v = edit::addTrack (c.project, TrackKind::Track, name);
        c.project.track (v)->armed = true;
        c.project.track (v)->armFor = ArmFor::MidiInput;
        voices.push_back (v);
    }
    c.project.device (dev)->midiTo = voices;
    // Kept in the song's file.
    auto saved = file::load (file::save (c.project));
    CHECK (saved.has_value());
    if (saved) CHECK (saved->device (dev)->midiTo == voices);

    c.focusTrack = chords;
    c.refresh();
    startLive (engine, device, c.project);
    c.refresh();
    c.cursor = 0;
    c.record();
    playLive (engine, 1.5);
    c.stop();
    MessageManager::getInstance()->runDispatchLoopUntil (50);

    const int want[] { 64, 60, 55, 48 };
    for (size_t i = 0; i < voices.size(); ++i)
    {
        const auto* t = c.project.track (voices[i]);
        int got = -1, count = 0;
        for (auto& cl : t->clips())
            for (auto& n : cl.midi.notes) { got = n.pitch; ++count; }
        if (count != 1 || got != want[i])
            check::fail (__FILE__, __LINE__, t->name + ": " + std::to_string (count) + " notes, pitch " + std::to_string (got));
    }
    // Its panel: an output each, top note first; fewer and more, one undo step each;
    // New Tracks makes a track for each output without one.
    auto* splitter = dynamic_cast<NoteSplitterProcessor*> (engine.instance (dev));
    CHECK (splitter != nullptr);
    if (! splitter) return;
    CHECK_EQ (NoteSplitterEditor::noteName (0, 4), String ("Top note"));
    CHECK_EQ (NoteSplitterEditor::noteName (1, 4), String ("2nd note down"));
    CHECK_EQ (NoteSplitterEditor::noteName (3, 4), String ("Bottom note"));
    NoteSplitterEditor panel (*splitter, c);
    const auto steps = c.history.size();
    c.setSplitterOutputs (dev, 6);
    CHECK_EQ (c.history.size(), steps + 1);
    CHECK_EQ (c.project.device (dev)->midiTo.size(), size_t (6));
    CHECK_EQ (splitter->outputCount.load(), 6);
    const auto tracksBefore = c.project.tracks.size();
    c.makeSplitterTracks (dev);
    CHECK_EQ (c.project.tracks.size(), tracksBefore + 2);
    const auto* made = c.project.track (c.project.device (dev)->midiTo[5]);
    CHECK (made != nullptr);
    if (made) CHECK_EQ (made->name, std::string ("Chords 6"));
    CHECK (c.undo());
    CHECK_EQ (c.project.tracks.size(), tracksBefore);
}

namespace
{
// The sloppy test library's own numbers (MorphProcessor.cpp's SloppyTest).
double sloppyHash (int n)
{
    uint32 x = (uint32) n ^ 0x9e3779b9u;
    x *= 0x85ebca6bu; x ^= x >> 13; x *= 0xc2b2ae35u; x ^= x >> 16;
    return x / 4294967296.0;
}
double sloppyOnsetMs (int p, int v)
{
    const double tau = std::acos (1 - 2 * std::pow (10.0, -12.0 / 20)) / MathConstants<double>::pi;
    return 45 + 70 * sloppyHash (p * 7 + 1) + (127 - v) * 0.12 + tau * (12 + 30 * sloppyHash (p * 13 + 5));
}

Id sloppyLine (Controller& c, Id& track)
{
    c.project = Project();
    track = edit::addTrack (c.project, TrackKind::Track, "Violins");
    edit::addDevice (c.project, track, PluginRef { "Kalibre", "test-sloppy", "Sloppy Library" }, DeviceRole::Instrument);
    const Id clip = edit::addMidiClip (c.project, track, PPQ * 4, PPQ * 12);
    const int line[] { 60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62, 60, 64, 67, 72, 67, 64 };
    for (int i = 0; i < 20; ++i)
    {
        Note n;
        n.start = (Tick) i * PPQ / 2;
        n.length = PPQ / 4;
        n.pitch = line[i];
        n.velocity = (int[]) { 70, 90, 110 }[i % 3];
        edit::addNote (c.project, clip, n);
    }
    c.refresh();
    return clip;
}
} // namespace

TEST ("fix note timings and volume: the track rendered quietly, each note given the delay that puts its sound on the grid, the loud and quiet keys evened out with CC7; one undo step (0085)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    Id t = 0;
    const Id clip = sloppyLine (c, t);
    const auto before = c.project;
    const auto steps = c.history.size();
    const auto temps = File::getSpecialLocation (File::tempDirectory).findChildFiles (File::findFiles, false, "Kalibre note fix*").size();
    CHECK (c.fixNoteTimingsAndVolume (clip, {}));
    CHECK_EQ (c.history.size(), steps + 1);
    CHECK_EQ (File::getSpecialLocation (File::tempDirectory).findChildFiles (File::findFiles, false, "Kalibre note fix*").size(), temps);   // the render is gone
    CHECK (c.message.contains ("Fixed"));
    const auto* cl = c.project.clip (clip);
    std::vector<double> err;
    for (auto& n : cl->midi.notes)
    {
        CHECK_EQ (n.start % (PPQ / 2), Tick (0));   // still on the grid
        err.push_back (std::abs (n.delayMs + sloppyOnsetMs (n.pitch, n.velocity)));
    }
    std::sort (err.begin(), err.end());
    CHECK (err[err.size() / 2] <= 3);
    CHECK (err.back() <= 12);
    // CC7 before each note: the loud keys (64, 71) turned down, the quiet one (67) up.
    auto ccBefore = [&] (const Note& n)
    {
        int v = -1;
        for (auto& e : cl->midi.events) if (e.type == EventType::Controller && e.number == 7 && e.tick <= n.start) v = e.value;
        return v;
    };
    for (auto& n : cl->midi.notes)
    {
        if (&n == &cl->midi.notes.front()) continue;   // sent before the clip begins: by velocity
        const int v = ccBefore (n);
        CHECK (v > 0);
        if (n.pitch == 64 || n.pitch == 71) CHECK (v < 90);
        if (n.pitch == 67) CHECK (v > 110);
    }
    // Again: little left to fix.
    auto delays = [&] { std::map<Id, double> d; for (auto& n : c.project.clip (clip)->midi.notes) d[n.id] = n.delayMs; return d; };
    const auto once = delays();
    CHECK (c.fixNoteTimingsAndVolume (clip, {}));
    std::vector<double> change;
    for (auto& [id, d] : delays()) change.push_back (std::abs (d - once.at (id)));
    std::sort (change.begin(), change.end());
    CHECK (change[change.size() / 2] <= 2);
    CHECK (c.undo());
    CHECK (c.undo());
    CHECK (c.project.clip (clip)->midi == before.clip (clip)->midi);
}

TEST ("fix note timings and volume: moving the notes instead, and velocity for the volume (0085)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    Id t = 0;
    const Id clip = sloppyLine (c, t);
    Controller::NoteFixOptions o;
    o.timing = Controller::NoteFixOptions::Timing::MoveNotes;
    o.volume = Controller::NoteFixOptions::Volume::Velocity;
    CHECK (c.fixNoteTimingsAndVolume (clip, o));
    const auto* cl = c.project.clip (clip);
    // The first note moved before the clip's old start: the clip starts earlier.
    CHECK (cl->start < PPQ * 4);
    // Each sound now heard on the grid it was written on (velocity moves a sample's delay a little).
    std::vector<double> err;
    const auto order = cl->midi.notes;
    for (size_t i = 0; i < order.size(); ++i)
    {
        const auto& n = order[i];
        CHECK_EQ (n.delayMs, 0.0);
        const double heardMs = c.project.tempo.secondsAt (cl->start - cl->offset + n.start) * 1000 + sloppyOnsetMs (n.pitch, n.velocity);
        double nearest = 1e9;
        for (int k = 0; k < 20; ++k) nearest = std::min (nearest, std::abs (heardMs - c.project.tempo.secondsAt (PPQ * 4 + k * PPQ / 2) * 1000));
        err.push_back (nearest);
    }
    std::sort (err.begin(), err.end());
    CHECK (err[err.size() / 2] <= 5);
    // Velocities changed for the faulty keys: 64 softer, 67 harder than they were.
    bool softer = false, harder = false;
    for (auto& n : cl->midi.notes)
    {
        const int was = (int[]) { 70, 90, 110 }[(n.start - cl->midi.notes.front().start + PPQ / 4) / (PPQ / 2) % 3];
        (void) was;
        if (n.pitch == 64 && n.velocity < 60) softer = true;    // 64 was played at 110, 70, 70, 90
        if (n.pitch == 67 && n.velocity > 95) harder = true;    // 67 at 90, 90, 90, 70
    }
    CHECK (softer && harder);
    CHECK (cl->midi.events.empty());
}

TEST ("fix note timings and volume: with a track delay, each note's own delay makes up the rest (0085)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    Id t = 0;
    const Id clip = sloppyLine (c, t);
    c.project.track (t)->delayMs = -50;
    c.refresh();
    Controller::NoteFixOptions o;
    o.volume = Controller::NoteFixOptions::Volume::Off;
    CHECK (c.fixNoteTimingsAndVolume (clip, o));
    std::vector<double> err;
    for (auto& n : c.project.clip (clip)->midi.notes) err.push_back (std::abs (-50 + n.delayMs + sloppyOnsetMs (n.pitch, n.velocity)));
    std::sort (err.begin(), err.end());
    CHECK (err[err.size() / 2] <= 3);
    CHECK (c.project.clip (clip)->midi.events.empty());
}

TEST ("tracks: at most 1000; the 1001st is refused with a message, whatever tried to make it (0088)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    for (int i = 0; i < 999; ++i) edit::addTrack (c.project, TrackKind::Track, "T" + std::to_string (i));
    c.refresh();
    const auto steps = c.history.size();
    CHECK (c.addTrack (TrackKind::Track) != 0);                 // the 1000th
    CHECK_EQ (c.project.tracks.size(), size_t (1000));
    CHECK_EQ (c.addTrack (TrackKind::Folder), Id (0));           // the 1001st
    CHECK_EQ (c.project.tracks.size(), size_t (1000));
    CHECK_EQ (c.message, String ("Unable to add more than 1000 tracks"));
    CHECK (c.messageIsWarning);
    CHECK_EQ (c.history.size(), steps + 1);                       // nothing to undo for it
    // A command that makes several (here the Note Splitter's New Tracks), all or nothing.
    const Id clipTrack = c.project.tracks.front().id;
    const Id dev = edit::addDevice (c.project, clipTrack, noteSplitterRef(), DeviceRole::MidiFx);
    c.refresh();
    c.makeSplitterTracks (dev);
    CHECK_EQ (c.project.tracks.size(), size_t (1000));
    CHECK_EQ (c.message, String ("Unable to add more than 1000 tracks"));
    // A script too.
    const auto script = folder().getChildFile ("add-a-track.lua");
    script.replaceWithText ("kalibre.addTrack (\"One Too Many\")\n");
    c.runScript (script);
    CHECK_EQ (c.project.tracks.size(), size_t (1000));
    CHECK_EQ (c.message, String ("Unable to add more than 1000 tracks"));
}

TEST ("fix note timings and volume: the clip's velocities and controllers don't sway what it hears; a dip written with CC7 is kept (0090)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    Id t = 0;
    const Id clip = sloppyLine (c, t);
    auto* cl = c.project.clip (clip);
    // The second note (D, a clean key) played quieter on purpose: CC7 50 for it, back to 100 after.
    const Tick second = cl->midi.notes[1].start;
    cl->midi.events.push_back ({ second - 10, EventType::Controller, 0, 7, 50 });
    cl->midi.events.push_back ({ second + PPQ / 4, EventType::Controller, 0, 7, 100 });
    c.refresh();
    std::map<Id, int> velocities;
    for (auto& n : cl->midi.notes) velocities[n.id] = n.velocity;
    CHECK (c.fixNoteTimingsAndVolume (clip, {}));
    cl = c.project.clip (clip);
    // Velocities as they were (but the first note's: pulled earlier, it's sent before the
    // clip begins, where no CC7 can go before it, so it is levelled by velocity, 0085).
    for (auto& n : cl->midi.notes) if (&n != &cl->midi.notes.front()) CHECK_EQ (n.velocity, velocities[n.id]);
    // The quiet note keeps its quiet level; the faulty keys are still found and evened out.
    auto ccFor = [&] (const Note& n)
    {
        int v = -1;
        Tick at = -1;
        for (auto& e : cl->midi.events) if (e.type == EventType::Controller && e.number == 7 && e.tick <= n.start && e.tick >= at) { v = e.value; at = e.tick; }
        return v;
    };
    const auto& d = cl->midi.notes[1];
    CHECK_EQ (d.pitch, 62);
    CHECK (std::abs (ccFor (d) - 50) <= 3);
    for (auto& n : cl->midi.notes)
    {
        if (&n == &cl->midi.notes.front()) continue;
        if (n.pitch == 64 || n.pitch == 71) CHECK (ccFor (n) < 90);
        if (n.pitch == 67) CHECK (ccFor (n) > 110);
    }
    // And the timing as before.
    std::vector<double> err;
    for (auto& n : cl->midi.notes) err.push_back (std::abs (n.delayMs + sloppyOnsetMs (n.pitch, n.velocity)));
    std::sort (err.begin(), err.end());
    CHECK (err[err.size() / 2] <= 3);
}

#include "TopBar.h"

TEST ("the timer: the song's music-making time beside the chord, counting while something happens, paused when nothing has for 3 minutes, saved with the song (0091)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    // A song saved after an hour and a minute of work.
    Project song;
    edit::addTrack (song, TrackKind::Track, "Piano");
    song.workSeconds = 3660;
    const auto f = folder().getChildFile ("timer.kalibre");
    f.replaceWithText (String (file::save (song)));
    CHECK (c.open (f));
    CHECK (std::abs (c.workClock.active - 3660) < 0.01);
    // Ten minutes of work, a tick every quarter second, then twenty minutes away.
    double t = Time::getMillisecondCounterHiRes() / 1000.0;
    c.beginWorkClock();
    for (int i = 0; i < 2400; ++i) { t += 0.25; if (i % 8 == 0 || i == 2399) c.noteActivity(); c.tickWorkClock (t); }
    CHECK (std::abs (c.workClock.active - (3660 + 600)) < 1);
    for (int i = 0; i < 4800; ++i) { t += 0.25; c.tickWorkClock (t); }
    CHECK (c.workClock.idle);
    CHECK (std::abs (c.workClock.active - (3660 + 600 + 180)) < 1);
    // An edit is someone there.
    c.apply ("Rename", [] (Project& p) { p.tracks.front().name = "Grand"; });
    t += 0.25;
    c.tickWorkClock (t);
    CHECK (! c.workClock.idle);
    // On the top bar, beside the chord.
    TopBar bar (c);
    bar.setSize (1600, 60);
    MessageManager::getInstance()->runDispatchLoopUntil (120);
    Component* timer = nullptr;
    Component* chord = nullptr;
    for (auto* ch : bar.getChildren())
    {
        auto* tip = dynamic_cast<TooltipClient*> (ch);
        if (! tip) continue;
        if (tip->getTooltip().startsWith ("How long you've been making music")) timer = ch;
        if (tip->getTooltip().startsWith ("The chord you're playing")) chord = ch;
    }
    CHECK (timer && chord);
    if (timer && chord)
    {
        CHECK (timer->isVisible());
        CHECK (timer->getX() > chord->getRight() && timer->getX() - chord->getRight() < 20);
    }
    // Saved with the song.
    CHECK (c.saveAs (f));
    auto back = file::load (f.loadFileAsString().toStdString());
    CHECK (back.has_value());
    if (back) CHECK (std::abs (back->workSeconds - c.workClock.active) < 0.01);
    // A new song starts at nothing.
    c.newProject();
    CHECK_EQ (c.workClock.active, 0.0);
}

TEST ("delay compensation: played live round a loop, a mute lane after a 300-sample plug-in follows the sound across the wrap; every pass exactly as long as the loop (0096)")
{
    AudioBuffer<float> dc (2, 96000);
    for (int ch = 0; ch < 2; ++ch) FloatVectorOperations::fill (dc.getWritePointer (ch), 0.5f, dc.getNumSamples());
    const auto f = folder().getChildFile ("dc-loop-late.wav");
    f.deleteFile();
    {
        WavAudioFormat wav;
        std::unique_ptr<OutputStream> os (f.createOutputStream().release());
        auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
        w->writeFromAudioSampleBuffer (dc, 0, dc.getNumSamples());
    }
    // A one-second loop (its end mid-block), and one exactly four blocks long (its end on a block's edge).
    for (auto [loopSamples, a] : std::vector<std::pair<int, Tick>> { { 48000, PPQ / 2 + 7 }, { 2048, 19 } })
    {
        PluginManager plugins;
        Engine engine (plugins);
        FakeDevice device;
        Project p;
        const Id t = edit::addTrack (p, TrackKind::Track, "DC");
        edit::addClip (p, t, audioClip (f, 0, PPQ * 4));
        p.track (t)->pan = 0;
        p.track (t)->volumeDb = 0;
        edit::addDevice (p, t, { "Kalibre", "test-latency:300", "Latency" }, DeviceRole::AudioFx);
        // Muted from the start of each pass until `a`.
        AutomationLane lane;
        lane.id = p.newId();
        lane.target.kind = AutoTarget::Kind::Mute;
        lane.points = { { 0, 1.0, 0, true }, { a, 0.0, 0, true } };
        p.track (t)->automation.push_back (lane);
        startLive (engine, device, p);
        engine.setLoop (true, 0.0, loopSamples / 48000.0);
        engine.locate (0.0);
        engine.play();
        const int passes = 3;
        const auto out = playLive (engine, (passes * loopSamples + 2000) / 48000.0);
        const int ka = (int) std::ceil (p.tempo.secondsAt (a) * 48000 - 1e-6);
        const int late = 300;   // the whole song comes out this much after it's played
        for (int pass = 1; pass < passes; ++pass)
        {
            const int o = pass * loopSamples + late;   // where this pass's first sample comes out
            CHECK_EQ (out.getSample (0, o - 1), 0.5f);      // the last pass, heard to its very end
            CHECK_EQ (out.getSample (0, o), 0.0f);          // muted from the very sample the pass starts
            CHECK_EQ (out.getSample (0, o + ka - 1), 0.0f);
            CHECK_EQ (out.getSample (0, o + ka), 0.5f);     // and heard again where the lane says
        }
        engine.stop();
        engine.audioDeviceStopped();
    }
}

TEST ("piano roll: in a short lower zone the notes keep the most room: at least 160 points, the lanes smaller (found in the UAT: nine keys)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Bass");
    const Id clip = edit::addMidiClip (c.project, t, 0, PPQ * 4);
    c.refresh();
    c.selectClip (clip);
    PianoRoll roll (c);
    for (int h : { 260, 300, 400, 700 })
    {
        roll.setSize (1100, h);
        const int notes = roll.notesShown().getHeight();
        CHECK (notes >= jmin (theme::px (160), h - theme::px (20) - theme::px (32)));
        CHECK (notes >= h / 2);   // the notes have at least half
    }
}

TEST ("track names: double-click opens the name to type, and it stays open while the arrangement refreshes (found in the UAT)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        const Id t = edit::addTrack (c.project, TrackKind::Track, "Track 1");
        c.refresh();
        Arrange arrange (c);
        arrange.setSize (1200, 500);
        arrange.resized();
        TrackHeader* header = nullptr;
        std::function<void (Component&)> find = [&] (Component& comp)
        {
            for (auto* ch : comp.getChildren())
            {
                if (auto* h = dynamic_cast<TrackHeader*> (ch); h && h->id == t) header = h;
                else find (*ch);
            }
        };
        find (arrange);
        CHECK (header != nullptr);
        if (! header) return;
        Label* name = nullptr;
        for (auto* ch : header->getChildren()) if (auto* l = dynamic_cast<Label*> (ch)) name = l;
        CHECK (name != nullptr);
        if (! name) return;
        name->showEditor();                                     // as the double-click does
        CHECK (name->isBeingEdited());
        // The first click chose the track; the arrangement refreshes after.
        c.chooseTrack (t, {}, c.arrangeOrder());
        arrange.resized();
        if (auto* tr = c.project.track (t)) header->update (*tr);
        CHECK (name->isBeingEdited());
        name->hideEditor (true);
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

TEST ("piano roll: a clip's notes wear its own track's colour, whichever track is chosen (found in the UAT)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        const Id bass = edit::addTrack (c.project, TrackKind::Track, "Bass");
        const Id chords = edit::addTrack (c.project, TrackKind::Track, "Chords");
        c.project.track (bass)->colour = 0xffe02020;     // red
        c.project.track (chords)->colour = 0xff2040e0;   // blue
        const Id clip = edit::addMidiClip (c.project, bass, 0, PPQ * 4);
        for (int i = 0; i < 4; ++i) { Note n; n.start = PPQ * i; n.length = PPQ; n.pitch = 60 + i; edit::addNote (c.project, clip, n); }
        c.refresh();
        c.selectClip (clip);
        c.focusTrack = chords;   // a track added or chosen since: the clip still open
        PianoRoll roll (c);
        roll.setSize (1100, 400);
        roll.showClip();
        const auto image = roll.createComponentSnapshot (roll.getLocalBounds());
        int red = 0, blue = 0;
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
            {
                const auto p = image.getPixelAt (x, y);
                red += p.getRed() > 150 && p.getBlue() < 100 && p.getGreen() < 100;
                blue += p.getBlue() > 150 && p.getRed() < 100;
            }
        CHECK (red > 200);
        CHECK (blue < red / 10);
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

#include "MorphProcessor.h"

TEST ("x/y morph: a corner's plug-in opens in a window of its own, from its letter or when loaded, never inside the device; it goes with its plug-in (0114)")
{
    PluginManager plugins;
    MorphProcessor morph (plugins);
    morph.setPlayConfigDetails (2, 2, 48000, 512);
    morph.prepareToPlay (48000, 512);
    String error;
    // Loaded with a song: no window by itself.
    CHECK (morph.loadPlugin (0, synthRef(), error));
    CHECK (! morph.cornerWindowOpen (0));

    // The device's window: the pad and four letters, no plug-in's window inside it.
    editors::Owned editor (editors::open (morph));
    CHECK (editor != nullptr);
    if (! editor) return;
    editor->setSize (360, 300);
    std::function<void (Component&, std::vector<Component*>&)> all = [&] (Component& comp, std::vector<Component*>& out)
    {
        out.push_back (&comp);
        for (auto* ch : comp.getChildren()) all (*ch, out);
    };
    std::vector<Component*> inside;
    all (*editor, inside);
    for (auto* comp : inside) CHECK (comp == editor.get() || dynamic_cast<AudioProcessorEditor*> (comp) == nullptr);
    Button* a = nullptr;
    for (auto* comp : inside) if (auto* b = dynamic_cast<Button*> (comp); b && b->getButtonText() == "A") a = b;
    CHECK (a != nullptr);
    if (! a) return;
    CHECK (! a->getToggleState());

    // Clicking A opens its window: the plug-in's own, in a window of its own.
    a->triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK (morph.cornerWindowOpen (0));
    auto* w = morph.cornerWindow (0);
    CHECK (w != nullptr);
    if (! w) return;
    CHECK (w->isVisible());
    CHECK (w->getName().contains ("A: Kalibre Synth"));
    CHECK (dynamic_cast<AudioProcessorEditor*> (w->getContentComponent()) != nullptr);
    CHECK (morph.corner (0).plugin->getActiveEditor() == w->getContentComponent());
    CHECK (a->getToggleState());
    // Its close button: gone, the plug-in told first.
    w->closeButtonPressed();
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK (! morph.cornerWindowOpen (0));
    CHECK (morph.corner (0).plugin->getActiveEditor() == nullptr);
    CHECK (! a->getToggleState());

    // Another plug-in in that corner, or Clear: the window goes before the plug-in does.
    morph.showCornerWindow (0);
    CHECK (morph.loadPlugin (0, samplerRef(), error));
    CHECK (! morph.cornerWindowOpen (0));
    morph.showCornerWindow (0);
    CHECK (morph.cornerWindowOpen (0));
    morph.clearCorner (0);
    CHECK (! morph.cornerWindowOpen (0));
    // Shut with the device's window and the corner still open: the device going takes it.
    morph.loadPlugin (1, synthRef(), error);
    morph.showCornerWindow (1);
    editor.reset();
    CHECK (morph.cornerWindowOpen (1));
}

TEST ("x/y morph: taken out of the song, its corners' windows close at once (0114)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Morph");
    c.refresh();
    Id dev = 0;
    c.apply ("Add", [&] (Project& p) { dev = edit::addDevice (p, t, morphRef(), DeviceRole::Instrument); });
    auto* morph = dynamic_cast<MorphProcessor*> (engine.instance (dev));
    CHECK (morph != nullptr);
    if (! morph) return;
    String error;
    CHECK (morph->loadPlugin (0, synthRef(), error));
    morph->showCornerWindow (0);
    CHECK (morph->cornerWindowOpen (0));
    Component::SafePointer<DocumentWindow> window (morph->cornerWindow (0));
    c.apply ("Remove", [&] (Project& p) { edit::removeDevice (p, dev); });
    CHECK (engine.instance (dev) == nullptr);
    // Closed at once: nothing shown, and its plug-in's window gone (the window itself waits, 0115).
    CHECK (window == nullptr || (! window->isVisible() && window->getContentComponent() == nullptr));
}

TEST ("windows: a plug-in's own window closed is emptied and hidden at once, and deleted only seconds later (the macOS crash in a scroll's glide, 0115)")
{
    const int was = editors::Retired::delayMs;
    editors::Retired::delayMs = 400;
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        const Id t = edit::addTrack (c.project, TrackKind::Track, "Synths");
        c.focusTrack = t;
        c.refresh();
        DeviceRack rack (c);
        rack.setSize (1400, 360);
        Id dev = 0;
        c.apply ("Add", [&] (Project& p) { dev = edit::addDevice (p, t, synthRef(), DeviceRole::Instrument); });
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        const int before = Desktop::getInstance().getNumComponents();
        rack.float_ (dev);
        CHECK_EQ (Desktop::getInstance().getNumComponents(), before + 1);
        auto* inst = engine.instance (dev);
        CHECK (inst != nullptr && inst->getActiveEditor() != nullptr);
        Component::SafePointer<Component> window (Desktop::getInstance().getComponent (Desktop::getInstance().getNumComponents() - 1));
        CHECK (dynamic_cast<DocumentWindow*> (window.getComponent()) != nullptr);
        rack.dock (dev);
        // The plug-in's window has gone; the window that held it waits, hidden and empty.
        CHECK (window != nullptr);
        if (auto* w = dynamic_cast<DocumentWindow*> (window.getComponent()))
        {
            CHECK (! w->isVisible());
            CHECK (w->getContentComponent() == nullptr);
        }
        MessageManager::getInstance()->runDispatchLoopUntil (900);
        CHECK (window == nullptr);
    }
    editors::Retired::delayMs = was;
}

TEST ("sampler: a sound dragged from the browser onto Kalibre Sampler in the rack loads it, handed on by its panel (the Mac's native views, 0116)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Sampler");
    c.focusTrack = t;
    c.refresh();
    DeviceRack rack (c);
    rack.setSize (1400, 360);
    OnScreen shown (rack);
    Id dev = 0;
    c.apply ("Add", [&] (Project& p) { dev = edit::addDevice (p, t, samplerRef(), DeviceRole::Instrument); });
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    auto* sampler = dynamic_cast<KalibreSampler*> (engine.instance (dev));
    CHECK (sampler != nullptr);
    if (! sampler) return;
    // The device's panel: the drop target Kalibre's drag finds when the window itself is out of its reach.
    DragAndDropTarget* panel = nullptr;
    std::function<void (Component&)> walk = [&] (Component& comp)
    {
        if (panel == nullptr && &comp != &rack && dynamic_cast<AudioProcessorEditor*> (&comp) == nullptr)
            if (auto* d = dynamic_cast<DragAndDropTarget*> (&comp)) panel = d;
        for (auto* ch : comp.getChildren()) walk (*ch);
    };
    walk (rack);
    CHECK (panel != nullptr);
    if (! panel) return;
    const auto wav = clickFile (0.25);
    DragAndDropTarget::SourceDetails d (var ("file:" + wav.getFullPathName()), nullptr, { 400, 200 });
    CHECK (panel->isInterestedInDragSource (d));
    panel->itemDropped (d);
    CHECK_EQ (sampler->file(), wav);
    CHECK_EQ (sampler->sample().getNumSamples() > 0, true);
}

namespace
{
// The sampler run for `seconds` with no notes: what its preview plays.
AudioBuffer<float> runSampler (KalibreSampler& s, double seconds, std::function<void (int block)> each = {})
{
    const int n = 480, blocks = (int) (seconds * 48000 / n);
    AudioBuffer<float> out (2, blocks * n), b (2, n);
    MidiBuffer midi;
    for (int k = 0; k < blocks; ++k)
    {
        if (each) each (k);
        b.clear();
        midi.clear();
        s.processBlock (b, midi);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, k * n, b, ch, 0, n);
    }
    return out;
}
} // namespace

TEST ("sampler: a click on the waveform plays the sample from there; a second click cuts the first off, never on top of it (0117)")
{
    KalibreSampler s;
    s.setPlayConfigDetails (0, 2, 48000, 480);
    s.prepareToPlay (48000, 480);
    String error;
    CHECK (s.load (clickFile (1.0), error));   // a click one second in, in two seconds
    // Clicked at a quarter (half a second in): the click is heard half a second later.
    s.audition (0.25f);
    auto out = runSampler (s, 1.0);
    const auto [at, level] = peak (out, 0);
    CHECK (std::abs (at - 24000) <= 2);
    CHECK (level > 0.3f);
    CHECK (s.auditionPosition() > 0.7f);   // still playing, three quarters of the way
    // From the start, then clicked again at three quarters before the click comes:
    // the first is cut off (its click, a second in, never heard); the second starts past it.
    s.reset();
    s.audition (0.0f);
    out = runSampler (s, 1.5, [&] (int block) { if (block == 50) s.audition (0.75f); });   // half a second in
    CHECK (peak (out, 0).second < 0.01f);
    // Back from a quarter again: one click, at the same place.
    s.reset();
    s.audition (0.25f);
    out = runSampler (s, 1.0);
    CHECK (std::abs (peak (out, 0).first - 24000) <= 2);
}

TEST ("sampler: Keys, Slices and Granular are buttons along its top; each shows its own knobs and hides the rest (0117)")
{
    KalibreSampler s;
    editors::Owned editor (editors::open (s));
    editor->setSize (1000, 300);
    auto visibleKnobs = [&]
    {
        StringArray names;
        std::function<void (Component&)> walk = [&] (Component& c)
        {
            if (auto* k = dynamic_cast<Slider*> (&c); k && k->isVisible()) names.add (k->getTitle());
            for (auto* ch : c.getChildren()) walk (*ch);
        };
        walk (*editor);
        return names;
    };
    auto button = [&] (const String& name) -> Button*
    {
        Button* found = nullptr;
        std::function<void (Component&)> walk = [&] (Component& c)
        {
            if (auto* b = dynamic_cast<Button*> (&c); b && b->getButtonText() == name) found = b;
            for (auto* ch : c.getChildren()) walk (*ch);
        };
        walk (*editor);
        return found;
    };
    for (auto* name : { "Keys", "Slices", "Granular" }) CHECK (button (name) != nullptr);
    if (! button ("Keys") || ! button ("Slices") || ! button ("Granular")) return;
    CHECK (button ("Keys")->getToggleState());
    auto keys = visibleKnobs();
    CHECK (keys.contains ("Loop") && keys.contains ("Root Note") && keys.contains ("Speed"));
    CHECK (! keys.contains ("Mode"));
    CHECK (! keys.contains ("Slice Sensitivity") && ! keys.contains ("Grain Size"));

    button ("Slices")->triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK (s.mode() == KalibreSampler::PlayMode::Slices);
    CHECK (button ("Slices")->getToggleState() && ! button ("Keys")->getToggleState());
    auto slices = visibleKnobs();
    CHECK (slices.contains ("Slice Sensitivity"));
    CHECK (! slices.contains ("Loop") && ! slices.contains ("Grain Size"));

    button ("Granular")->triggerClick();
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    CHECK (s.mode() == KalibreSampler::PlayMode::Granular);
    auto grains = visibleKnobs();
    for (auto* k : { "Grain Size", "Density", "Spray", "Position", "Loop" }) CHECK (grains.contains (k));
    CHECK (! grains.contains ("Slice Sensitivity"));
}

TEST ("arrangement: scrolled down, the tracks' headers never cover the Sections and Chords lanes under the ruler (0120)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    for (int i = 0; i < 12; ++i) edit::addTrack (c.project, TrackKind::Track, "Track " + std::to_string (i + 1));
    c.showSectionLane = c.showChordLane = true;
    c.refresh();
    Arrange arrange (c);
    arrange.setSize (1200, 500);
    arrange.resized();
    // Scrolled down by a track and a half.
    arrange.mouseWheelMove (MouseEvent (Desktop::getInstance().getMainMouseSource(), { 600.0f, 300.0f }, {}, 1.0f, 0, 0, 0, 0,
                                        &arrange, &arrange, Time::getCurrentTime(), { 600.0f, 300.0f }, Time::getCurrentTime(), 1, false),
                            MouseWheelDetails { 0.0f, -0.3f, false, false, false });
    const int lanesBottom = arrange.rulerHeight();
    CHECK (lanesBottom > 60);
    int shown = 0;
    std::function<void (Component&)> walk = [&] (Component& comp)
    {
        for (auto* ch : comp.getChildren())
        {
            if (auto* h = dynamic_cast<TrackHeader*> (ch); h && h->isVisible())
            {
                ++shown;
                // Where it's drawn in the arrangement, cut to what shows of it.
                const auto onScreen = arrange.getLocalArea (h, h->getLocalBounds())
                                          .getIntersection (arrange.getLocalArea (h->getParentComponent(), h->getParentComponent()->getLocalBounds()));
                CHECK (onScreen.isEmpty() || onScreen.getY() >= lanesBottom);
            }
            else walk (*ch);
        }
    };
    walk (arrange);
    CHECK (shown > 0);
}

TEST ("playhead: between the audio's blocks it moves on with the clock, evenly, not in a block's steps; it never runs far ahead of the audio (0121)")
{
    PluginManager plugins;
    Engine engine (plugins);
    FakeDevice device;
    Project p;
    edit::addTrack (p, TrackKind::Track, "Keys");
    startLive (engine, device, p);
    engine.locate (0);
    engine.play();
    playLive (engine, 0.05);   // a few blocks
    const double heard = engine.heardPosition();
    const double t0 = Time::getMillisecondCounterHiRes();
    const double a = engine.smoothPosition();
    Thread::sleep (30);
    const double b = engine.smoothPosition();
    const double gone = (Time::getMillisecondCounterHiRes() - t0) / 1000.0;   // however long the sleep really was
    // No block since: the block-by-block position hasn't moved, the smooth one has, by the time gone.
    CHECK (std::abs (engine.heardPosition() - heard) < 1.0e-12);
    CHECK (b > a);
    if (gone < 0.2) CHECK (std::abs ((b - a) - gone) < 0.005);
    // The audio stops coming: the playhead stops a quarter of a second on, at most.
    Thread::sleep (400);
    CHECK (engine.smoothPosition() - a <= 0.26);
    engine.stop();
    CHECK (std::abs (engine.smoothPosition() - engine.position()) < 1.0e-12);
}
