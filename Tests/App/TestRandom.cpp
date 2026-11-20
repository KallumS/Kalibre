// The Random button (decision 0101): a whole song by chance, that plays.
#include "Check.h"
#include "BuiltinInstruments.h"
#include "Controller.h"
#include "MainComponent.h"
#include "TopBar.h"
#include "Theme.h"

using namespace kal;
using namespace juce;

namespace
{
File soundsFolder()
{
    auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("kalibre-random-sounds");
    f.deleteRecursively();
    f.createDirectory();
    return f;
}

// A WAV of `seconds`, `fn` of time, mono, 16-bit (not Kalibre's own format: it's copied as one).
File writeSound (const File& dir, const String& name, double seconds, std::function<float (double)> fn)
{
    const auto f = dir.getChildFile (name);
    AudioBuffer<float> b (1, (int) (seconds * 44100));
    for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, fn (i / 44100.0));
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os (f.createOutputStream().release());
    auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (44100).withNumChannels (1).withBitsPerSample (16));
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    return f;
}
} // namespace

TEST ("random: the Random button's song has 4 to 8 tracks, every instrument loaded, loops from the sounds, and it plays (0101)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.newProject();
    // The musician's sounds: a drum loop, a tonal one-shot.
    const auto dir = soundsFolder();
    const double pi = MathConstants<double>::pi;
    const auto loop = writeSound (dir, "Groove 100bpm.wav", 4.8, [pi] (double t) { return (float) (0.7 * std::exp (-std::fmod (t * 100 / 60, 1.0) * 8) * std::sin (2 * pi * 60 * t)); });
    const auto pluck = writeSound (dir, "Pluck.wav", 0.6, [pi] (double t) { return (float) (0.6 * std::exp (-t * 6) * std::sin (2 * pi * 220 * t)); });
    library::Index index;
    for (auto [f, bpm, seconds] : std::vector<std::tuple<File, double, double>> { { loop, 100.0, 4.8 }, { pluck, 0.0, 0.6 } })
    {
        library::Item it;
        it.path = f.getFullPathName().toStdString();
        it.seconds = seconds;
        it.sampleRate = 44100;
        it.channels = 1;
        it.bpm = bpm;
        it.bpmFrom = bpm > 0 ? library::Item::TempoFrom::Name : library::Item::TempoFrom::Unknown;
        index.items[it.path] = it;
    }

    bool sawLoop = false, sawSampler = false;
    for (unsigned seed = 1; seed <= 12 && ! (sawLoop && sawSampler); ++seed)
    {
        CHECK (c.randomSong (index, seed));
        const auto& p = c.project;
        CHECK (p.tracks.size() >= 4 && p.tracks.size() <= 8);
        CHECK (! c.history.canUndo());               // a new song, as File > New
        CHECK (c.message.contains ("random song"));
        CHECK_EQ (c.lastRandomParts.size(), p.tracks.size());
        int effects = 0;
        for (auto& t : p.tracks)
        {
            for (auto& d : t.devices)
            {
                CHECK (engine.instance (d.id) != nullptr);   // loaded
                effects += d.role == DeviceRole::AudioFx;
                if (d.plugin.name == "Kalibre Sampler")
                {
                    sawSampler = true;
                    auto* s = dynamic_cast<KalibreSampler*> (engine.instance (d.id));
                    CHECK (s != nullptr && s->sample().getNumSamples() > 0);   // its sound loaded
                }
            }
            for (auto& cl : t.clips())
                if (! cl.isMidi())
                {
                    sawLoop = true;
                    CHECK (File (String (cl.audio.file)).existsAsFile());
                    CHECK (File (String (cl.audio.file)) != loop);   // copied as Kalibre's 32-bit float WAV
                }
        }
        CHECK (effects <= 8);
    }
    CHECK (sawLoop);
    CHECK (sawSampler);

    // It plays: four seconds of it, exported, aren't silent.
    Engine::RenderOptions o;
    o.start = 0; o.end = 4.0; o.tail = 0; o.dither = false; o.bitDepth = 32;
    const auto out = dir.getChildFile ("random.wav");
    CHECK (c.exportAudio (out, o, false));
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (out));
    CHECK (r != nullptr);
    if (r)
    {
        AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
        r->read (&b, 0, b.getNumSamples(), 0, true, true);
        CHECK (b.getMagnitude (0, b.getNumSamples()) > 0.01f);
    }
}

TEST ("random: the Random button is at the top right of the top bar, and asks to save a changed song first (0101)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.newProject();
        MainComponent w (c);
        w.setSize (1440, 900);
        Button* random = nullptr;
        // The top bar's (the pattern editors have Random buttons of their own).
        for (auto* ch : w.getChildren())
            if (dynamic_cast<TopBar*> (ch))
                for (auto* b : ch->getChildren())
                    if (auto* btn = dynamic_cast<Button*> (b); btn && btn->getButtonText() == "Random") random = btn;
        CHECK (random != nullptr);
        if (random)
        {
            const auto at = w.getLocalArea (random->getParentComponent(), random->getBounds());
            CHECK (at.getRight() > w.getWidth() - 40);
            CHECK (at.getY() < 80);
            // A changed song: the question comes first, and the song stays until it's answered.
            c.apply ("Change", [] (Project& p) { p.tempo.setTempoAt (0, 99); });
            random->triggerClick();
            MessageManager::getInstance()->runDispatchLoopUntil (50);
            AlertWindow* asked = nullptr;
            for (auto* ch : w.getChildren()) if (auto* a = dynamic_cast<AlertWindow*> (ch)) asked = a;
            CHECK (asked != nullptr);
            CHECK_EQ (c.project.tempo.bpmAt (0), 99.0);
            if (asked) asked->exitModalState (2);   // Don't Save: the random song comes
            for (int i = 0; i < 100 && c.project.tracks.size() < 4; ++i) MessageManager::getInstance()->runDispatchLoopUntil (20);
            CHECK (c.project.tracks.size() >= 4);
        }
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

#include "Edit.h"
#include "ProjectFile.h"

TEST ("files: opening a song never keeps the last song's plug-ins, though their devices have the same ids (0101)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    // Two songs, each a track with Kalibre Synth as its device 2, set differently.
    auto song = [] (float cutoff)
    {
        Project p;
        const Id t = edit::addTrack (p, TrackKind::Track, "Synth");
        const Id d = edit::addDevice (p, t, synthRef(), DeviceRole::Instrument);
        KalibreSynth s;
        s.getParameters()[KalibreSynth::Cutoff]->setValue (cutoff);
        MemoryBlock m;
        s.getStateInformation (m);
        p.device (d)->state = m.toBase64Encoding().toStdString();
        return std::make_pair (p, d);
    };
    auto [a, da] = song (0.2f);
    auto [b, db] = song (0.8f);
    CHECK_EQ (da, db);   // the same id in each
    const auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("kalibre-open-two");
    dir.createDirectory();
    const auto fa = dir.getChildFile ("A.kalibre"), fb = dir.getChildFile ("B.kalibre");
    fa.replaceWithText (String (file::save (a)));
    fb.replaceWithText (String (file::save (b)));
    CHECK (c.open (fa));
    CHECK (std::abs (engine.instance (da)->getParameters()[KalibreSynth::Cutoff]->getValue() - 0.2f) < 0.01f);
    CHECK (c.open (fb));
    CHECK (std::abs (engine.instance (db)->getParameters()[KalibreSynth::Cutoff]->getValue() - 0.8f) < 0.01f);   // B's, not A's
    // And a new song starts with none of them.
    c.newProject();
    CHECK (engine.instance (db) == nullptr);
}
