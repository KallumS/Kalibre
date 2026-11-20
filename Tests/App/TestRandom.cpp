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

TEST ("random: the plug-ins scanned are in the pool, but not those that measure, nor Kalibre's own twice (0102)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    auto scanned = [&] (const String& name, bool instrument, const String& category = {})
    {
        PluginDescription d;
        d.name = name;
        d.pluginFormatName = "VST3";
        d.fileOrIdentifier = "/Library/Audio/Plug-Ins/VST3/" + name + ".vst3";
        d.manufacturerName = "Someone";
        d.category = category;
        d.isInstrument = instrument;
        d.numInputChannels = instrument ? 0 : 2;
        d.numOutputChannels = 2;
        plugins.known().addType (d);
    };
    scanned ("Lush Pads", true);
    scanned ("Big Drums", true, "Instrument|Drum");
    scanned ("Warm Delay", false, "Fx|Delay");
    scanned ("Spectrum Analyzer", false, "Fx|Analyzer");
    const auto cat = c.randomCatalogue ({});
    auto has = [] (const std::vector<randomsong::Choice>& v, const char* name, bool* drums = nullptr)
    {
        for (auto& x : v) if (x.name == name) { if (drums) *drums = x.drums; return true; }
        return false;
    };
    bool drums = false;
    CHECK (has (cat.instruments, "Lush Pads"));
    CHECK (has (cat.instruments, "Big Drums", &drums) && drums);
    CHECK (has (cat.effects, "Warm Delay"));
    CHECK (! has (cat.effects, "Spectrum Analyzer"));
    CHECK (has (cat.instruments, "Kalibre Synth"));   // with Kalibre's own
    // Not left in this Mac's list of scanned plug-ins for the next test, or the app.
    for (auto* name : { "Lush Pads", "Big Drums", "Warm Delay", "Spectrum Analyzer" })
        for (auto& d : plugins.known().getTypes()) if (d.name == name) plugins.known().removeType (d);
}

TEST ("random: a scanned plug-in that can't be loaded (gone since its scan) is left out, in the same undo step (0102)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.newProject();
    PluginDescription d;
    d.name = "Gone Reverb";
    d.pluginFormatName = "VST3";
    d.fileOrIdentifier = "/nowhere/Gone Reverb.vst3";
    d.numInputChannels = d.numOutputChannels = 2;
    plugins.known().addType (d);
    const Id t = c.project.tracks.front().id;
    const auto before = c.history.size();
    bool sawGone = false;
    for (unsigned seed = 1; seed <= 30; ++seed)
    {
        CHECK (c.randomDevices (t, seed));
        for (auto& x : c.project.track (t)->devices) CHECK (engine.instance (x.id) != nullptr);
        sawGone |= c.message.contains ("couldn't be loaded");
        c.undo();
    }
    CHECK (sawGone);
    CHECK_EQ (c.history.size(), before);
    plugins.known().removeType (d);
}

TEST ("random: the device rack's Random adds an instrument where notes have none, one or two effects, one undo step; a return gets effects (0102)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.newProject();
    const Id t = c.project.tracks.front().id;
    const auto before = c.history.size();
    CHECK (c.randomDevices (t, 11));
    CHECK_EQ (c.history.size(), before + 1);
    int instruments = 0, effects = 0;
    for (auto& d : c.project.track (t)->devices)
    {
        instruments += d.role == DeviceRole::Instrument;
        effects += d.role == DeviceRole::AudioFx;
        CHECK (engine.instance (d.id) != nullptr);
    }
    CHECK_EQ (instruments, 1);
    CHECK (effects >= 1 && effects <= 2);
    CHECK (c.message.contains ("Random devices"));
    // Again: it has an instrument now; only effects (and perhaps a MIDI effect) come.
    CHECK (c.randomDevices (t, 12));
    instruments = 0;
    for (auto& d : c.project.track (t)->devices) instruments += d.role == DeviceRole::Instrument;
    CHECK_EQ (instruments, 1);
    c.undo();
    c.undo();
    CHECK (c.project.track (t)->devices.empty());
    // A return: effects only.
    const Id r = c.addTrack (TrackKind::Return);
    CHECK (c.randomDevices (r, 13));
    for (auto& d : c.project.track (r)->devices) CHECK (d.role == DeviceRole::AudioFx);
}

#include "TopBar.h"

TEST ("random: the Scale menu's Random Scale chooses another root and scale, one undo step (0102)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.newProject();
    TopBar bar (c);
    const auto was = c.project.scale;
    const auto before = c.history.size();
    bar.randomScale (7);
    CHECK (! (c.project.scale.root == was.root && c.project.scale.scale == was.scale));
    CHECK_EQ (c.history.size(), before + 1);
    CHECK (c.message.contains ("The scale is now"));
    c.undo();
    CHECK (c.project.scale == was);
}

TEST ("random: the master gets nothing by chance: the rack's Random isn't offered there (0102)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.newProject();
    const auto before = c.history.size();
    CHECK (! c.randomDevices (c.project.master.id, 5));
    CHECK (c.project.master.devices.empty());
    CHECK_EQ (c.history.size(), before);
    // The top bar's Random leaves the master as it is too.
    CHECK (c.randomSong ({}, 5));
    CHECK (c.project.master.devices.empty());
}

TEST ("song versions: 1 and 2 sit left of Random in the top bar; clicking 2 makes it, then each click chooses; 2 is hidden with the rest (0103)")
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
        TopBar* bar = nullptr;
        for (auto* ch : w.getChildren()) if (auto* b = dynamic_cast<TopBar*> (ch)) bar = b;
        CHECK (bar != nullptr);
        if (bar)
        {
            Button* random = nullptr;
            for (auto* b : bar->getChildren())
                if (auto* btn = dynamic_cast<Button*> (b); btn && btn->getButtonText() == "Random") random = btn;
            CHECK (random != nullptr);
            CHECK (bar->songOne.isVisible() && bar->songTwo.isVisible());
            CHECK (bar->songOne.getRight() < bar->songTwo.getX());
            if (random) CHECK (bar->songTwo.getRight() < random->getX());
            CHECK (bar->songOne.getY() < 48);
            CHECK (bar->songOne.getToggleState() && ! bar->songTwo.getToggleState());

            bar->songTwo.triggerClick();
            MessageManager::getInstance()->runDispatchLoopUntil (50);
            CHECK_EQ (c.project.songVersions(), 2);
            CHECK_EQ (c.project.songVersion, 1);
            CHECK (bar->songTwo.getToggleState() && ! bar->songOne.getToggleState());
            bar->songOne.triggerClick();
            MessageManager::getInstance()->runDispatchLoopUntil (50);
            CHECK_EQ (c.project.songVersion, 0);
            CHECK (c.message.contains ("Song version 1"));

            c.hiddenInTopBar.add ("Song Versions");
            bar->resized();
            CHECK (! bar->songOne.isVisible() && ! bar->songTwo.isVisible());
        }
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}

namespace
{
std::vector<float> valuesOf (AudioPluginInstance& inst)
{
    std::vector<float> v;
    for (auto* p : inst.getParameters()) v.push_back (p->getValue());
    return v;
}
} // namespace

TEST ("random settings: a device's Random sets its controls by chance, but not volume, tuning or pitch; one undo step; nothing touched (0104)")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    c.project = Project();
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Keys");
    const Id synth = edit::addDevice (c.project, t, synthRef(), DeviceRole::Instrument);
    const Id u = edit::addTrack (c.project, TrackKind::Track, "Sampler");
    const Id sampler = edit::addDevice (c.project, u, samplerRef(), DeviceRole::Instrument);
    c.refresh();
    for (Id dev : { synth, sampler })
    {
        auto* inst = engine.instance (dev);
        CHECK (inst != nullptr);
        if (! inst) continue;
        const auto was = valuesOf (*inst);
        const auto steps = c.history.size();
        const int changed = c.randomSettings (dev, 21);
        CHECK (changed > 0);
        CHECK_EQ (c.history.size(), steps + 1);
        CHECK (c.lastTouched.device != dev);   // set by chance, not touched
        const auto now = valuesOf (*inst);
        int differ = 0;
        for (size_t i = 0; i < now.size(); ++i)
        {
            const auto name = inst->getParameters()[(int) i]->getName (100);
            const bool guarded = randomsong::keepsItsValue (name.toStdString());
            if (guarded) CHECK (now[i] == was[i]);
            else differ += now[i] != was[i];
        }
        CHECK (differ >= changed / 2);
        // Volume, tuning and pitch by name, as the musician sees them.
        for (auto* p : inst->getParameters())
            for (auto* name : { "Volume", "Tune", "Root Note", "Speed", "Osc 2 Pitch", "Osc 2 Fine" })
                if (p->getName (100) == name) CHECK (p->getValue() == was[(size_t) p->getParameterIndex()]);
        CHECK (c.message.contains ("left as they were"));
        c.undo();
        const auto back = valuesOf (*engine.instance (dev));
        CHECK_EQ (back.size(), was.size());
        float worst = 0;
        for (size_t i = 0; i < back.size() && i < was.size(); ++i) worst = std::max (worst, std::abs (back[i] - was[i]));
        if (worst > 1e-5f) std::printf ("    undo: off by %g\n", worst);
        CHECK (worst <= 1e-5f);
    }
}

#include "DeviceRack.h"

TEST ("random settings: the Random button sits in each plug-in's header, beside B (0104)")
{
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    {
        PluginManager plugins;
        Engine engine (plugins);
        Controller c (engine);
        c.project = Project();
        const Id t = edit::addTrack (c.project, TrackKind::Track, "Keys");
        edit::addDevice (c.project, t, synthRef(), DeviceRole::Instrument);
        c.focusTrack = t;
        c.refresh();
        DeviceRack rack (c);
        rack.setSize (1200, 400);
        rack.rebuild();
        rack.resized();
        std::function<Button* (Component&, const String&)> find = [&] (Component& comp, const String& title) -> Button*
        {
            for (auto* ch : comp.getChildren())
            {
                if (auto* b = dynamic_cast<Button*> (ch); b && b->getTitle() == title) return b;
                if (auto* b = find (*ch, title)) return b;
            }
            return nullptr;
        };
        auto* random = find (rack, "Random Settings");
        auto* bypass = find (rack, "Bypass");
        CHECK (random != nullptr && bypass != nullptr);
        if (random && bypass)
        {
            CHECK (random->isEnabled());
            CHECK (random->getY() == bypass->getY());
            CHECK (std::abs (random->getRight() - bypass->getX()) <= 6);
            const auto steps = c.history.size();
            random->triggerClick();
            MessageManager::getInstance()->runDispatchLoopUntil (50);
            CHECK_EQ (c.history.size(), steps + 1);
        }
    }
    LookAndFeel::setDefaultLookAndFeel (nullptr);
}
