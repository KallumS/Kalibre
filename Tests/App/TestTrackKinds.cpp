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
    c.apply ("Add", [t] (Project& p) { edit::addDevice (p, t, synthRef(), DeviceRole::Instrument); edit::addDevice (p, t, samplerRef(), DeviceRole::Instrument); });
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    auto editorsIn = [] (Component& root)
    {
        std::vector<AudioProcessorEditor*> found;
        std::function<void (Component&)> walk = [&] (Component& comp)
        {
            if (auto* e = dynamic_cast<AudioProcessorEditor*> (&comp)) found.push_back (e);
            for (auto* child : comp.getChildren()) walk (*child);
        };
        walk (root);
        return found;
    };
    const auto before = editorsIn (rack);
    CHECK_EQ (before.size(), size_t (2));
    c.apply ("Add", [t] (Project& p) { edit::addDevice (p, t, synthRef(), DeviceRole::Instrument); });
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
    c.apply ("Add", [t] (Project& p) { edit::addDevice (p, t, synthRef(), DeviceRole::Instrument); });
    MessageManager::getInstance()->runDispatchLoopUntil (50);

    AudioProcessorEditor* editor = nullptr;
    std::function<void (Component&)> walk = [&] (Component& comp)
    {
        if (auto* e = dynamic_cast<AudioProcessorEditor*> (&comp)) editor = e;
        for (auto* child : comp.getChildren()) walk (*child);
    };
    walk (rack);
    CHECK (editor != nullptr);
    if (! editor) return;
    auto shownIn = [&]
    {
        rack.resized();
        return rack.getLocalArea (editor, editor->getLocalBounds().toFloat());
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
    CHECK (std::abs (shown.getWidth() - (float) editor->getWidth()) < 1.0f);
    CHECK (std::abs (shown.getHeight() - (float) editor->getHeight()) < 1.0f);
}
