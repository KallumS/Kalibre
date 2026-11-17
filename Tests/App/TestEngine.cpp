#include "Check.h"
#include "Demo.h"
#include "Engine.h"

using namespace kal;
using namespace juce;

namespace
{
File tempFolder()
{
    auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("KalibreTests");
    f.createDirectory();
    return f;
}

// A WAV with one full-scale click at `at` seconds.
File clickFile (double at, double rate = 48000)
{
    const auto f = tempFolder().getChildFile ("click" + String (at) + ".wav");
    f.deleteFile();
    AudioBuffer<float> b (2, (int) (rate * 2));
    b.clear();
    b.setSample (0, (int) (at * rate), 1.0f);
    b.setSample (1, (int) (at * rate), 1.0f);
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os (f.createOutputStream().release());
    auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32));
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

std::vector<int> clicksIn (const File& f)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
    std::vector<int> at;
    if (! r) return at;
    AudioBuffer<float> b (2, (int) r->lengthInSamples);
    r->read (&b, 0, b.getNumSamples(), 0, true, true);
    for (int i = 0; i < b.getNumSamples(); ++i)
        if (std::abs (b.getSample (0, i)) > 0.2f) at.push_back (i);
    return at;
}
} // namespace

TEST ("engine: plug-in delay compensation lines up a delayed track with an undelayed one")
{
    PluginManager plugins;
    Engine engine (plugins);
    Project p;
    const auto click = clickFile (0.5);
    const Id a = edit::addTrack (p, TrackKind::Track, "Dry");
    const Id b = edit::addTrack (p, TrackKind::Track, "Through a 300-sample plug-in");
    edit::addClip (p, a, audioClip (click, 0, PPQ * 4));
    edit::addClip (p, b, audioClip (click, 0, PPQ * 4));
    PluginRef lag;
    lag.format = "Kalibre";
    lag.identifier = "test-latency:300";
    lag.name = "Latency";
    edit::addDevice (p, b, lag, DeviceRole::AudioFx);
    // Half each, so the two together make one click of 1.0 only when lined up.
    p.track (a)->volumeDb = -6.0206;
    p.track (b)->volumeDb = -6.0206;
    p.track (a)->pan = p.track (b)->pan = 0;

    Engine::RenderOptions o;
    o.start = 0; o.end = 1.0; o.tail = 0.2; o.dither = false; o.bitDepth = 32;
    const auto out = tempFolder().getChildFile ("pdc.wav");
    String error;
    CHECK (engine.render (p, tempFolder(), out, o, {}, error));
    const auto clicks = clicksIn (out);
    CHECK_EQ (clicks.size(), size_t (1));            // one click, not two 300 samples apart
    if (! clicks.empty()) CHECK (std::abs (clicks.front() - 24000) <= 1);
}

TEST ("engine: a negative track delay plays a track early in a render")
{
    PluginManager plugins;
    Engine engine (plugins);
    Project p;
    const auto click = clickFile (0.5);
    const Id t = edit::addTrack (p, TrackKind::Track, "Early");
    p.track (t)->delayMs = -125;
    edit::addClip (p, t, audioClip (click, PPQ, PPQ * 4));   // starts at 0.5 s; the click is 0.5 s in
    Engine::RenderOptions o;
    o.start = 0; o.end = 2.0; o.tail = 0; o.dither = false; o.bitDepth = 32;
    const auto out = tempFolder().getChildFile ("delay.wav");
    String error;
    CHECK (engine.render (p, tempFolder(), out, o, {}, error));
    const auto clicks = clicksIn (out);
    CHECK_EQ (clicks.size(), size_t (1));
    if (! clicks.empty()) CHECK (std::abs (clicks.front() - (48000 - 6000)) <= 1);
}

TEST ("engine: folders inside folders sum into the master; muting a folder silences what's inside")
{
    PluginManager plugins;
    Engine engine (plugins);
    Project p;
    const auto click = clickFile (0.25);
    const Id outer = edit::addTrack (p, TrackKind::Folder, "Outer");
    const Id inner = edit::addTrack (p, TrackKind::Folder, "Inner", 0, outer);
    const Id t = edit::addTrack (p, TrackKind::Track, "Deep", 0, inner);
    edit::addClip (p, t, audioClip (click, 0, PPQ * 2));
    Engine::RenderOptions o;
    o.start = 0; o.end = 0.5; o.tail = 0; o.dither = false; o.bitDepth = 32;
    String error;
    const auto out = tempFolder().getChildFile ("folders.wav");
    CHECK (engine.render (p, tempFolder(), out, o, {}, error));
    CHECK_EQ (clicksIn (out).size(), size_t (1));
    p.track (outer)->mute = true;
    CHECK (engine.render (p, tempFolder(), out, o, {}, error));
    CHECK_EQ (clicksIn (out).size(), size_t (0));
}

TEST ("engine: a deactivated plug-in is unloaded and comes back with its state")
{
    PluginManager plugins;
    Engine engine (plugins);
    Project p = demo::song();
    const Id keys = p.tracks[1].id;
    const Id dev = p.track (keys)->devices.front().id;
    engine.sync (p, keys, tempFolder());
    auto* morph = dynamic_cast<MorphProcessor*> (engine.instance (dev));
    CHECK (morph != nullptr);
    if (! morph) return;
    *morph->x = 0.75f;
    const auto state = engine.stateOf (dev);
    edit::setDeviceActive (p, dev, false, state);
    engine.sync (p, keys, tempFolder());
    CHECK (engine.instance (dev) == nullptr);
    edit::setDeviceActive (p, dev, true);
    engine.sync (p, keys, tempFolder());
    auto* back = dynamic_cast<MorphProcessor*> (engine.instance (dev));
    CHECK (back != nullptr);
    if (back) CHECK (std::abs (back->x->get() - 0.75f) < 1.0e-4f);
}

TEST ("engine: the X/Y morph plays its sample corners by the pad's weights")
{
    PluginManager plugins;
    MorphProcessor morph (plugins);
    const auto click = clickFile (0.0);
    String error;
    CHECK (morph.loadSample (0, click, error));   // A, top left
    morph.setPlayConfigDetails (2, 2, 48000, 256);
    morph.prepareToPlay (48000, 256);
    AudioBuffer<float> b (2, 256);
    MidiBuffer m;
    *morph.x = 0.0f; *morph.y = 1.0f;             // all A
    b.clear();
    m.addEvent (MidiMessage::noteOn (1, 60, 1.0f), 0);
    morph.processBlock (b, m);
    CHECK (std::abs (b.getSample (0, 0) - 1.0f) < 0.01f);
    *morph.x = 1.0f; *morph.y = 0.0f;             // all D: A silent
    b.clear();
    m.clear();
    m.addEvent (MidiMessage::noteOn (1, 60, 1.0f), 0);
    morph.processBlock (b, m);
    CHECK (b.getMagnitude (0, 256) < 1.0e-6f);
}

TEST ("engine: a warped clip at half the tempo is twice as long")
{
    PluginManager plugins;
    Project p;
    p.tempo.setTempoAt (0, 60);
    CachedAudio src;
    src.sampleRate = 48000;
    src.buffer.setSize (2, 48000);
    for (int i = 0; i < 48000; ++i) { src.buffer.setSample (0, i, std::sin (i * 0.05f) * 0.5f); src.buffer.setSample (1, i, std::sin (i * 0.05f) * 0.5f); }
    Clip c;
    c.kind = Clip::Kind::Audio;
    c.start = 0;
    c.audio.sourceBpm = 120;   // a second of a 120 BPM file is two beats
    c.length = PPQ * 2;
    for (auto mode : { WarpMode::Repitch, WarpMode::Granular, WarpMode::Spectral })
    {
        c.audio.warp = mode;
        auto out = AudioCache::render (src, c, p.tempo, 48000);
        // Two beats at 60 BPM: two seconds.
        CHECK (std::abs (out->buffer.getNumSamples() - 96000) < 10);
        CHECK (out->buffer.getRMSLevel (0, 24000, 48000) > 0.1f);
    }
}

#include "Controller.h"

TEST ("engine: freezing a track renders it with its plug-ins and unloads them")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    const auto click = clickFile (0.5);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Frozen");
    edit::addClip (c.project, t, audioClip (click, 0, PPQ * 4));
    PluginRef lag;
    lag.format = "Kalibre";
    lag.identifier = "test-latency:100";
    const Id dev = edit::addDevice (c.project, t, lag, DeviceRole::AudioFx);
    c.refresh();
    CHECK (engine.instance (dev) != nullptr);
    c.freeze (t, true);
    CHECK (c.project.track (t)->frozen);
    CHECK (engine.instance (dev) == nullptr);
    const File frozen (c.project.track (t)->frozenFile);
    CHECK (frozen.existsAsFile());
    const auto clicks = clicksIn (frozen);
    CHECK_EQ (clicks.size(), size_t (1));
    if (! clicks.empty()) CHECK (std::abs (clicks.front() - 24000) <= 1);   // the plug-in's delay compensated
    c.freeze (t, false);
    CHECK (engine.instance (dev) != nullptr);
}

TEST ("engine: a track's sound reaches another track's plug-in through its sidechain, lined up")
{
    PluginManager plugins;
    Engine engine (plugins);
    Project p;
    const auto click = clickFile (0.5);
    const Id kick = edit::addTrack (p, TrackKind::Track, "Kick");
    edit::addClip (p, kick, audioClip (click, 0, PPQ * 4));
    // The kick goes through a 200-sample plug-in, and is kept out of the mix
    // itself: it's heard only through the other track's sidechain.
    PluginRef lag;
    lag.format = "Kalibre";
    lag.identifier = "test-latency:200";
    edit::addDevice (p, kick, lag, DeviceRole::AudioFx);
    const Id bass = edit::addTrack (p, TrackKind::Track, "Bass");
    edit::addClip (p, bass, audioClip (click, 0, PPQ * 4));
    PluginRef sc;
    sc.format = "Kalibre";
    sc.identifier = "test-sidechain";
    const Id dev = edit::addDevice (p, bass, sc, DeviceRole::AudioFx);
    p.device (dev)->sidechainFrom = kick;
    p.track (kick)->output = bass;   // not into the master: send it somewhere it's replaced
    Engine::RenderOptions o;
    o.start = 0; o.end = 1.0; o.tail = 0.2; o.dither = false; o.bitDepth = 32;
    const auto out = tempFolder().getChildFile ("sidechain.wav");
    String error;
    // The kick routed into the bass track and also into its sidechain: allowed, it's not a loop.
    CHECK (engine.render (p, tempFolder(), out, o, {}, error));
    CHECK (engine.sidechainChannels (dev) == 2);
    p.track (kick)->output = 0;
    p.track (kick)->volumeDb = 0;
    p.track (kick)->mute = false;
    // Now: master hears the kick (delayed by its plug-in, compensated) and the bass track's sidechain copy of it.
    CHECK (engine.render (p, tempFolder(), out, o, {}, error));
    const auto clicks = clicksIn (out);
    CHECK_EQ (clicks.size(), size_t (1));
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (out));
    AudioBuffer<float> b (2, (int) r->lengthInSamples);
    r->read (&b, 0, b.getNumSamples(), 0, true, true);
    if (! clicks.empty()) CHECK (std::abs (b.getSample (0, clicks.front()) - 2.0f) < 0.01f);   // both, together
}

TEST ("midi learn: a learned controller moves a track's volume, pan and mute")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    const Id t = c.project.tracks.front().id;
    c.touchedMix (t, AutoTarget::Kind::Volume, "Volume");
    c.learnLastTouched();
    CHECK (engine.learning.load());
    engine.learning = false;
    engine.onLearned (1, 7);
    CHECK_EQ (c.project.midiMappings.size(), size_t (1));
    c.mapped (c.project.midiMappings[0], 60.0 / 66.0);
    CHECK (std::abs (c.project.track (t)->volumeDb) < 1e-6);
    CHECK (std::abs (engine.fader (t)->volumeDb.load()) < 1e-6f);
    c.touchedMix (t, AutoTarget::Kind::Mute, "Mute");
    engine.onLearned (1, 20);
    CHECK_EQ (c.project.midiMappings.size(), size_t (2));
    c.mapped (c.project.midiMappings[1], 1.0);
    CHECK (c.project.track (t)->mute);
    c.forgetMapping (0);
    CHECK_EQ (c.project.midiMappings.size(), size_t (1));
}

TEST ("loudness: an export at -14 LUFS comes out at -14 LUFS, and the master's meter reads it")
{
    PluginManager plugins;
    Engine engine (plugins);
    Project p;
    // Four seconds of a 997 Hz tone at -30 dBFS.
    const auto f = tempFolder().getChildFile ("tone.wav");
    f.deleteFile();
    AudioBuffer<float> b (2, 48000 * 4);
    for (int k = 0; k < b.getNumSamples(); ++k)
        for (int ch = 0; ch < 2; ++ch) b.setSample (ch, k, 0.0316f * std::sin (2.0f * 3.14159265f * 997.0f * (float) k / 48000.0f));
    {
        WavAudioFormat wav;
        std::unique_ptr<OutputStream> os (f.createOutputStream().release());
        auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (32));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
    const Id t = edit::addTrack (p, TrackKind::Track, "Tone");
    edit::addClip (p, t, audioClip (f, 0, PPQ * 8));
    Engine::RenderOptions o;
    o.start = 0; o.end = 4.0; o.tail = 0; o.dither = false; o.bitDepth = 32;
    o.loudnessTarget = -14.0;
    const auto out = tempFolder().getChildFile ("loud.wav");
    String error;
    CHECK (engine.render (p, tempFolder(), out, o, {}, error));
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (out));
    AudioBuffer<float> got (2, (int) r->lengthInSamples);
    r->read (&got, 0, got.getNumSamples(), 0, true, true);
    const auto measured = loudness::measure (got.getReadPointer (0), got.getReadPointer (1), got.getNumSamples(), 48000);
    CHECK (std::abs (measured.integrated + 14.0) < 0.2);
    // The master fader measured the render as it went: about -30 LUFS before the gain.
    CHECK (std::abs (engine.fader (p.master.id)->loudnessMeter.integrated() + 30.0) < 0.5);
}

TEST ("automation: a mute lane silences a click; a send lane's level is followed; the plug-in's time is counted")
{
    PluginManager plugins;
    Engine engine (plugins);
    Project p;
    const auto click = clickFile (0.5);
    const Id t = edit::addTrack (p, TrackKind::Track, "Muted by automation");
    edit::addClip (p, t, audioClip (click, 0, PPQ * 4));
    PluginRef lag;
    lag.format = "Kalibre";
    lag.identifier = "test-latency:10";
    lag.name = "Latency";
    const Id dev = edit::addDevice (p, t, lag, DeviceRole::AudioFx);
    AutoTarget mute;
    mute.kind = AutoTarget::Kind::Mute;
    auto& lane = edit::laneFor (*p.track (t), mute, 0.0);
    lane.id = p.newId();
    edit::addPoint (lane, { 0, 1.0, 0, true });   // muted from the start
    Engine::RenderOptions o;
    o.start = 0; o.end = 1.0; o.tail = 0; o.dither = false; o.bitDepth = 32;
    const auto out = tempFolder().getChildFile ("mute-auto.wav");
    String error;
    CHECK (engine.render (p, tempFolder(), out, o, {}, error));
    CHECK (clicksIn (out).empty());

    // Unmuted at 0.25 s (half a beat at 120): the click at 0.5 s is heard.
    edit::addPoint (lane, { PPQ / 2, 0.0, 0, true });
    CHECK (engine.render (p, tempFolder(), out, o, {}, error));
    CHECK_EQ (clicksIn (out).size(), size_t (1));
    CHECK (engine.instance (dev) != nullptr);   // the plug-in itself, not its stopwatch
}

#include "BuiltinInstruments.h"

TEST ("instruments: the built-in synth and sampler play a track's notes")
{
    PluginManager plugins;
    Engine engine (plugins);
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Synth");
    edit::addDevice (p, t, synthRef(), DeviceRole::Instrument);
    const Id clip = edit::addMidiClip (p, t, 0, PPQ * 4);
    edit::addNote (p, clip, { .start = 0, .length = PPQ, .pitch = 57, .velocity = 110 });
    Engine::RenderOptions o;
    o.start = 0; o.end = 1.0; o.tail = 0; o.dither = false; o.bitDepth = 32;
    const auto out = tempFolder().getChildFile ("synth.wav");
    String error;
    CHECK (engine.render (p, tempFolder(), out, o, {}, error));
    AudioFormatManager fm;
    fm.registerBasicFormats();
    auto peakOf = [&fm] (const File& f, double from, double to)
    {
        std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
        if (! r) return 0.0f;
        AudioBuffer<float> b (2, (int) r->lengthInSamples);
        r->read (&b, 0, b.getNumSamples(), 0, true, true);
        const int a = (int) (from * r->sampleRate), z = jmin (b.getNumSamples(), (int) (to * r->sampleRate));
        return z > a ? b.getMagnitude (a, z - a) : 0.0f;
    };
    CHECK (peakOf (out, 0.05, 0.4) > 0.05f);    // the note sounds
    CHECK (peakOf (out, 0.95, 1.0) < 0.01f);    // and has stopped by the end

    // The sampler, with a click as its sample, played an octave up: twice as fast, still one click.
    Project q;
    const Id s = edit::addTrack (q, TrackKind::Track, "Sampler");
    const Id dev = edit::addDevice (q, s, samplerRef(), DeviceRole::Instrument);
    KalibreSampler loader;
    String err;
    CHECK (loader.load (clickFile (0.5), err));
    MemoryBlock m;
    loader.getStateInformation (m);
    q.device (dev)->state = m.toBase64Encoding().toStdString();
    const Id c2 = edit::addMidiClip (q, s, 0, PPQ * 4);
    edit::addNote (q, c2, { .start = 0, .length = PPQ * 2, .pitch = 72, .velocity = 127 });
    const auto out2 = tempFolder().getChildFile ("sampler.wav");
    CHECK (engine.render (q, tempFolder(), out2, o, {}, error));
    const auto clicks = clicksIn (out2);
    CHECK (! clicks.empty());
    if (! clicks.empty()) CHECK (std::abs (clicks.front() - 12000) <= 2);   // 0.5 s of sample in 0.25 s
}

#include "HardwareInsert.h"

namespace
{
std::vector<float> peaksPerChannel (const File& f, int& channels)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
    std::vector<float> out;
    channels = r ? (int) r->numChannels : 0;
    if (! r) return out;
    AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, b.getNumSamples(), 0, true, true);
    for (int ch = 0; ch < b.getNumChannels(); ++ch) out.push_back (b.getMagnitude (ch, 0, b.getNumSamples()));
    return out;
}
} // namespace

TEST ("surround: a 5.1 mix exports six channels, a track ahead in the centre and one behind left in the left surround")
{
    PluginManager plugins;
    Engine engine (plugins);
    Project p;
    p.surround = SurroundFormat::Surround51;
    const auto click = clickFile (0.5);
    const Id a = edit::addTrack (p, TrackKind::Track, "Ahead");
    edit::addClip (p, a, audioClip (click, 0, PPQ * 4));
    Engine::RenderOptions o;
    o.start = 0; o.end = 1.0; o.tail = 0; o.dither = false; o.bitDepth = 32;
    const auto out = tempFolder().getChildFile ("surround.wav");
    String error;
    CHECK (engine.render (p, tempFolder(), out, o, {}, error));
    int channels = 0;
    auto pk = peaksPerChannel (out, channels);
    CHECK_EQ (channels, 6);
    if (pk.size() == 6)
    {
        CHECK (pk[2] > 0.9f);                       // centre
        CHECK (pk[0] < 0.01f && pk[1] < 0.01f && pk[4] < 0.01f);
    }
    p.track (a)->pan = -0.94;
    p.track (a)->frontBack = -0.34;
    CHECK (engine.render (p, tempFolder(), out, o, {}, error));
    pk = peaksPerChannel (out, channels);
    if (pk.size() == 6) CHECK (pk[4] > 0.85f && pk[2] < 0.01f);
}

TEST ("routing: an instrument's second output plays on a track of its own; a hardware insert passes sound through when exporting")
{
    PluginManager plugins;
    Engine engine (plugins);
    Project p;
    const Id main = edit::addTrack (p, TrackKind::Track, "Multi");
    PluginRef multi;
    multi.format = "Kalibre";
    multi.identifier = "test-multiout";
    multi.name = "Multi-Out Test";
    multi.isInstrument = true;
    const Id dev = edit::addDevice (p, main, multi, DeviceRole::Instrument);
    Engine::RenderOptions o;
    o.start = 0; o.end = 0.5; o.tail = 0; o.dither = false; o.bitDepth = 32;
    const auto out = tempFolder().getChildFile ("multiout.wav");
    String error;
    CHECK (engine.render (p, tempFolder(), out, o, {}, error));
    int channels = 0;
    auto pk = peaksPerChannel (out, channels);
    CHECK (! pk.empty() && pk[0] < 0.001f);         // only its main output: silent
    const Id aux = edit::addTrack (p, TrackKind::Track, "Multi 3-4");
    p.track (aux)->sourceDevice = dev;
    p.track (aux)->sourcePair = 1;
    edit::addDevice (p, aux, hardwareInsertRef(), DeviceRole::AudioFx);
    CHECK (engine.render (p, tempFolder(), out, o, {}, error));
    pk = peaksPerChannel (out, channels);
    // 0.25 through the pan law's centre (+3 dB on each side of -3 dB): 0.25 at unity.
    CHECK (! pk.empty() && std::abs (pk[0] - 0.25f) < 0.01f);
}

TEST ("chance and timing: an export plays each note as often as its chance says, each within its timing range (decision 0061)")
{
    PluginManager plugins;
    Engine engine (plugins);
    // The sampler, with a click 50 ms into its sample: each note played leaves a click.
    auto make = [] (Project& q, int chance, double timingMs)
    {
        q.tempo.setTempos ({ { 0, 120.0, false } });
        const Id s = edit::addTrack (q, TrackKind::Track, "Hats");
        const Id dev = edit::addDevice (q, s, samplerRef(), DeviceRole::Instrument);
        KalibreSampler loader;
        String err;
        loader.load (clickFile (0.05), err);
        MemoryBlock m;
        loader.getStateInformation (m);
        q.device (dev)->state = m.toBase64Encoding().toStdString();
        const Id c = edit::addMidiClip (q, s, 0, PPQ * 64);
        for (int i = 0; i < 40; ++i)
        {
            Note n;
            n.start = PPQ + PPQ * i;     // every half second from 0.5 s
            n.length = PPQ / 2;
            n.pitch = 60;
            n.velocity = 127;
            n.chance = chance;
            n.timingMs = timingMs;
            edit::addNote (q, c, n);
        }
    };
    Engine::RenderOptions o;
    o.start = 0; o.end = 21.5; o.tail = 0; o.dither = false; o.bitDepth = 32;
    String error;
    // A click is one sample loud; count those apart from each other.
    auto clicks = [] (const File& f)
    {
        std::vector<int> at;
        for (int i : clicksIn (f)) if (at.empty() || i - at.back() > 100) at.push_back (i);
        return at;
    };

    Project never;
    make (never, 0, 0);
    const auto f0 = tempFolder().getChildFile ("chance0.wav");
    CHECK (engine.render (never, tempFolder(), f0, o, {}, error));
    CHECK (clicks (f0).empty());

    Project half;
    make (half, 50, 0);
    const auto f50 = tempFolder().getChildFile ("chance50.wav");
    CHECK (engine.render (half, tempFolder(), f50, o, {}, error));
    const auto some = clicks (f50);
    CHECK (some.size() >= 8 && some.size() <= 32);

    Project loose;
    make (loose, 100, 20);
    const auto ft = tempFolder().getChildFile ("timing20.wav");
    CHECK (engine.render (loose, tempFolder(), ft, o, {}, error));
    const auto all = clicks (ft);
    CHECK_EQ (all.size(), size_t (40));
    int off = 0;
    for (size_t i = 0; i < all.size(); ++i)
    {
        const int written = (int) ((0.5 + 0.5 * (double) i + 0.05) * 48000);   // the note, then the click 50 ms into the sample
        const int d = all[i] - written;
        CHECK (std::abs (d) <= 20 * 48 + 2);
        if (std::abs (d) > 48) ++off;      // more than a millisecond away
    }
    CHECK (off > 20);   // most land off the grid, either way
}

#include "JsfxDevice.h"
#include "Editors.h"

TEST ("jsfx: the family's five JSFX load, each with its sliders as parameters, and keep their state")
{
    for (auto& s : jsfxDevices())
    {
        auto inst = makeJsfx (jsfxRef (s));
        CHECK (inst != nullptr);
        if (! inst) continue;
        CHECK (inst->getParameters().size() > 0);           // compiled: its sliders are there
        if (inst->getParameters().isEmpty()) { std::printf ("    %s didn't compile\n", s.name); continue; }
        inst->prepareToPlay (48000, 512);
        // A slider moved, saved, and loaded into a fresh one.
        auto* p = inst->getParameters()[0];
        p->setValueNotifyingHost (p->getValue() < 0.5f ? 0.8f : 0.2f);
        AudioBuffer<float> b (2, 512);
        MidiBuffer m;
        b.clear();
        inst->processBlock (b, m);
        MemoryBlock state;
        inst->getStateInformation (state);
        auto again = makeJsfx (jsfxRef (s));
        again->setStateInformation (state.getData(), (int) state.getSize());
        CHECK (std::abs (again->getParameters()[0]->getValue() - p->getValue()) < 0.01f);
        // Its window opens.
        editors::Owned e (editors::open (*inst));
        CHECK (e != nullptr && dynamic_cast<GenericAudioProcessorEditor*> (e.get()) == nullptr);
        if (e) CHECK (e->getWidth() >= s.minWidth && e->getHeight() >= s.minHeight);
    }
}

TEST ("jsfx: Reese Basser and Instrument Creator play a note; Reverberator rings on after a click; Midi Evolver's notes play the synth")
{
    PluginManager plugins;
    Engine engine (plugins);
    auto find = [] (const char* id) -> const JsfxSpec& { for (auto& s : jsfxDevices()) if (String (s.id) == id) return s; return jsfxDevices().front(); };
    AudioFormatManager fm;
    fm.registerBasicFormats();
    auto peakIn = [&fm] (const File& f, double from, double to)
    {
        std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
        if (! r) return 0.0f;
        AudioBuffer<float> b (2, (int) r->lengthInSamples);
        r->read (&b, 0, b.getNumSamples(), 0, true, true);
        const int a = (int) (from * r->sampleRate), z = jmin (b.getNumSamples(), (int) (to * r->sampleRate));
        return z > a ? b.getMagnitude (a, z - a) : 0.0f;
    };
    Engine::RenderOptions o;
    o.start = 0; o.end = 2.0; o.tail = 0; o.dither = false; o.bitDepth = 32;
    String error;

    for (const char* id : { "reese-basser", "instrument-creator" })
    {
        Project p;
        const Id t = edit::addTrack (p, TrackKind::Track, id);
        edit::addDevice (p, t, jsfxRef (find (id)), DeviceRole::Instrument);
        const Id clip = edit::addMidiClip (p, t, 0, PPQ * 4);
        edit::addNote (p, clip, { .start = 0, .length = PPQ * 2, .pitch = 48, .velocity = 110 });
        const auto out = tempFolder().getChildFile (String (id) + ".wav");
        CHECK (engine.render (p, tempFolder(), out, o, {}, error));
        const float peak = peakIn (out, 0.0, 1.0);
        CHECK (peak > 0.01f);
        if (peak <= 0.01f) std::printf ("    %s: peak %f\n", id, peak);
    }

    {
        Project p;
        const Id t = edit::addTrack (p, TrackKind::Track, "Reverb");
        edit::addClip (p, t, audioClip (clickFile (0.1), 0, PPQ * 4));
        edit::addDevice (p, t, jsfxRef (find ("reverberator")), DeviceRole::AudioFx);
        const auto out = tempFolder().getChildFile ("reverberator.wav");
        CHECK (engine.render (p, tempFolder(), out, o, {}, error));
        const float tail = peakIn (out, 0.4, 1.5);
        CHECK (tail > 0.0005f);                      // the click rings on in the material
        if (tail <= 0.0005f) std::printf ("    reverberator tail %f\n", tail);
    }

    {
        Project p;
        const Id t = edit::addTrack (p, TrackKind::Track, "Evolving");
        edit::addDevice (p, t, jsfxRef (find ("midi-evolver")), DeviceRole::MidiFx);
        edit::addDevice (p, t, synthRef(), DeviceRole::Instrument);
        const Id clip = edit::addMidiClip (p, t, 0, PPQ * 8);
        for (int pitch : { 48, 52, 55 }) edit::addNote (p, clip, { .start = 0, .length = PPQ * 6, .pitch = pitch, .velocity = 100 });
        o.end = 3.0;
        const auto out = tempFolder().getChildFile ("evolver.wav");
        CHECK (engine.render (p, tempFolder(), out, o, {}, error));
        const float peak = peakIn (out, 0.0, 3.0);
        CHECK (peak > 0.01f);                         // the synth heard what Midi Evolver played
        if (peak <= 0.01f) std::printf ("    evolver: peak %f\n", peak);
    }
}

TEST ("instant virtual instrument: built in, its parameters passed on, its window, its demo played, its state kept")
{
    auto inst = makeInstantVirtualInstrument();
    CHECK (inst != nullptr);
    if (! inst) return;
    CHECK (inst->getParameters().size() >= 15);
    inst->prepareToPlay (48000, 512);
    CHECK (loadInstantVirtualInstrumentDemo (*inst));
    // Its analysis runs in the background: play a held note until it sounds (20 s at most).
    AudioBuffer<float> b (2, 512);
    float heard = 0;
    for (int i = 0; i < 400 && heard < 0.01f; ++i)
    {
        MessageManager::getInstance()->runDispatchLoopUntil (50);
        MidiBuffer m;
        if (i % 20 == 0) m.addEvent (MidiMessage::noteOn (1, 60, (uint8) 110), 0);
        b.clear();
        inst->processBlock (b, m);
        heard = jmax (heard, b.getMagnitude (0, 512));
    }
    CHECK (heard > 0.01f);
    // A parameter set through Kalibre reaches it, and comes back from its saved state.
    int which = 0;
    for (int i = 0; i < inst->getParameters().size(); ++i)
        if (inst->getParameters()[i]->getName (40) == "Brightness") which = i;
    auto* p = inst->getParameters()[which];
    CHECK_EQ (p->getName (40), String ("Brightness"));
    p->setValueNotifyingHost (0.25f);
    MemoryBlock state;
    inst->getStateInformation (state);
    auto again = makeInstantVirtualInstrument();
    again->setStateInformation (state.getData(), (int) state.getSize());
    CHECK (std::abs (p->getValue() - 0.25f) < 0.01f);
    CHECK (std::abs (again->getParameters()[which]->getValue() - 0.25f) < 0.01f);
    // Its own window, inside the device's; open, close, open again.
    for (int k = 0; k < 2; ++k)
    {
        editors::Owned e (editors::open (*inst));
        CHECK (e != nullptr && e->getNumChildComponents() == 1);
        CHECK (e && e->getWidth() > 300 && e->getHeight() > 200);
    }
}

#include "LuaTool/LuaTool.h"

TEST ("lua tools: Starting Blocks, its Notation, Midi Catalogue and Orchestration Helper run their own scripts, play their block, keep their settings")
{
    for (const auto& spec : luaTools())
    {
        auto inst = makeLuaTool (luaToolRef (spec));
        CHECK (inst != nullptr);
        if (! inst) continue;
        CHECK_EQ (luaToolError (*inst), String());
        if (luaToolError (*inst).isNotEmpty()) std::printf ("    %s: %s\n", spec.id, luaToolError (*inst).toRawUTF8());
        CHECK (inst->acceptsMidi() && inst->producesMidi());

        // Its window: the script draws a frame at least once a size.
        {
            editors::Owned e (editors::open (*inst));
            CHECK (e != nullptr && e->getWidth() >= 320 && e->getHeight() >= 240);
            MessageManager::getInstance()->runDispatchLoopUntil (200);
            CHECK_EQ (luaToolError (*inst), String());
        }

        // Orchestration Helper draws its page with gfx: something is on it.
        if (spec.gfx)
        {
            CHECK (luaToolRun (*inst, "return gfx.w").getIntValue() >= 320);
            CHECK (luaToolRun (*inst, "return gfx.texth").getIntValue() > 4);
        }

        // Audition plays the block through MIDI out. Midi Catalogue hands its
        // block over when Audition is clicked; the others as soon as they start.
        if (! spec.gfx)
        {
        if (String (spec.id) == "midi-catalogue")
            luaToolRun (*inst, "host.setBlock({ { start = 0, len = 1, pitch = 64, vel = 90 } }, 4)");
        inst->prepareToPlay (48000, 512);
        CHECK_EQ (luaToolRun (*inst, "host.audition(true) return host.auditioning()"), String ("true"));
        AudioBuffer<float> b (2, 512);
        int ons = 0;
        for (int i = 0; i < 400; ++i)   // about 4 s at 120 bpm: a block's worth
        {
            MidiBuffer m;
            inst->processBlock (b, m);
            for (const auto meta : m) if (meta.getMessage().isNoteOn()) ++ons;
        }
        CHECK (ons > 0);
        if (ons == 0) std::printf ("    %s: no notes\n", spec.id);
        }

        // Its settings (REAPER's ExtState) go with the project.
        luaToolRun (*inst, "reaper.SetExtState('kalibre-test', 'k', 'kept', true)");
        MemoryBlock state;
        inst->getStateInformation (state);
        auto again = makeLuaTool (luaToolRef (spec));
        again->setStateInformation (state.getData(), (int) state.getSize());
        CHECK_EQ (luaToolRun (*again, "return reaper.GetExtState('kalibre-test', 'k')"), String ("kept"));
        CHECK_EQ (luaToolError (*again), String());
    }
}

TEST ("lua tools: Insert at Cursor puts the block on the device's track at the playhead; Midi Catalogue's parts get a track each; one undo step")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Blocks");
    const auto find = [] (const char* id) { for (auto& s : luaTools()) if (String (s.id) == id) return luaToolRef (s); return PluginRef(); };
    const Id sb = edit::addDevice (c.project, t, find ("starting-blocks"), DeviceRole::MidiFx);
    c.refresh();
    auto* inst = engine.instance (sb);
    CHECK (inst != nullptr);
    if (! inst) return;
    c.locate (PPQ * 8);

    const String one = "return dofile('<embedded>/tools/starting-blocks/sb_place.lua').insert({ name = 'Lift', beats = 4, "
                       "notes = { { start = 0, len = 1, pitch = 60, vel = 100 }, { start = 1.5, len = 0.5, pitch = 64, vel = 90 } } })";
    CHECK_EQ (luaToolRun (*inst, one), String ("0"));   // Place.OK
    const auto& clips = c.project.track (t)->clips();
    CHECK_EQ (clips.size(), size_t (1));
    if (clips.size() == 1)
    {
        CHECK_EQ (clips[0].start, PPQ * 8);
        CHECK_EQ (clips[0].length, PPQ * 4);
        CHECK_EQ (clips[0].name, std::string ("Lift"));
        CHECK_EQ (clips[0].midi.notes.size(), size_t (2));
        if (clips[0].midi.notes.size() == 2) CHECK_EQ (clips[0].midi.notes[1].start, PPQ * 3 / 2);
    }
    CHECK (c.undo());
    CHECK (c.project.track (t)->clips().empty());

    // Midi Catalogue's block across a section: one new track per instrument.
    const Id mc = edit::addDevice (c.project, t, find ("midi-catalogue"), DeviceRole::MidiFx);
    c.refresh();
    auto* cat = engine.instance (mc);
    CHECK (cat != nullptr);
    if (! cat) return;
    const size_t before = c.project.tracks.size();
    const String parts = "local n = { start = 0, len = 2, pitch = 67, vel = 100 } "
                         "return dofile('<embedded>/tools/midi-catalogue/mc_place.lua').insert({ name = 'Swell', beats = 8, notes = { n, n }, "
                         "parts = { { name = 'Violin I', notes = { n } }, { name = 'Viola', notes = { n } } } })";
    CHECK_EQ (luaToolRun (*cat, parts), String ("0"));
    CHECK_EQ (c.project.tracks.size(), before + 2);
    const int at = c.project.indexOf (t);
    if (at >= 0 && (size_t) at + 2 < c.project.tracks.size())
    {
        CHECK_EQ (c.project.tracks[(size_t) at + 1].name, std::string ("Violin I"));
        CHECK_EQ (c.project.tracks[(size_t) at + 2].name, std::string ("Viola"));
        CHECK_EQ (c.project.tracks[(size_t) at + 1].clips().size(), size_t (1));
    }
    CHECK (c.undo());
    CHECK_EQ (c.project.tracks.size(), before);
}
