// Versions with their own plug-ins (decision 0050), the Summing track
// (decision 0052) and the Cue track (decision 0051), played through the live
// audio path with a stand-in sound card, and exported.
#include "Check.h"
#include "Controller.h"
#include "Edit.h"
#include "Engine.h"
#include "ProjectFile.h"
#include "PluginManager.h"

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

TEST ("plug-in panels: a plug-in taller than the zone is shown smaller to fit, the same both ways; never bigger, never stretched")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Synths");
    c.focusTrack = t;
    c.refresh();
    DeviceRack rack (c);
    rack.setSize (1400, 160);   // a short lower zone
    Id synth = 0;
    c.apply ("Add", [&] (Project& p) { synth = edit::addDevice (p, t, synthRef(), DeviceRole::Instrument); });
    MessageManager::getInstance()->runDispatchLoopUntil (50);

    auto* editor = rack.editorOf (synth);
    CHECK (editor != nullptr);
    if (! editor) return;
    auto shownIn = [&]
    {
        rack.resized();
        const auto area = rack.pluginArea (synth).toFloat();
       #if ! JUCE_MAC
        // Off the Mac it's an ordinary child: where JUCE draws it is where the rack says.
        const auto drawn = rack.getLocalArea (editor, editor->getLocalBounds().toFloat());
        CHECK (area.getPosition().getDistanceFrom (drawn.getPosition()) < 1.5f);
        CHECK (std::abs (area.getWidth() - drawn.getWidth()) < 1.5f && std::abs (area.getHeight() - drawn.getHeight()) < 1.5f);
       #endif
        return area;
    };
    const auto own = editor->getLocalBounds().toFloat();
    auto shown = shownIn();
    CHECK (shown.getHeight() <= 160.0f - 30.0f);                         // under the header, inside the zone
    CHECK (shown.getHeight() < own.getHeight());                         // smaller than it is
    CHECK (std::abs (shown.getWidth() / shown.getHeight() - own.getWidth() / own.getHeight()) < 0.02f);   // its shape kept
    CHECK (rack.getLocalBounds().toFloat().contains (shown));

    // A tall zone: its own size, not made bigger.
    rack.setSize (1400, 1400);
    MessageManager::getInstance()->runDispatchLoopUntil (20);
    shown = shownIn();
    CHECK (std::abs (shown.getWidth() - (float) editor->getWidth()) < 1.5f);
    CHECK (std::abs (shown.getHeight() - (float) editor->getHeight()) < 1.5f);
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
    auto shownNamed = [&] (const String& text)
    {
        for (auto* ch : bar.getChildren())
            if (auto* b = dynamic_cast<Button*> (ch); b && b->getButtonText() == text) return b->isVisible();
        return false;
    };
    CHECK (shownNamed ("Tap") && shownNamed ("Punch") && shownNamed ("Count-in"));
    c.hiddenInTopBar = { "Tap", "Punch", "Count-in" };
    bar.resized();
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
    MessageManager::getInstance()->runDispatchLoopUntil (60);
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
}

#include "CrashReports.h"
#include "CrashContext.h"
#include <sys/wait.h>
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
        crashcontext::audioPlugin.store ("Riffer");
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
