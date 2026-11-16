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
