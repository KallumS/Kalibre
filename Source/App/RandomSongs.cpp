// The Random button's song (decision 0101): what Kalibre has to choose from,
// the song made by Core's RandomSong, its sounds made ready, put in its place.
#include "Controller.h"

#include "AudioFiles.h"
#include "BuiltinInstruments.h"
#include "Edit.h"
#include "JsfxDevice.h"
#include "DrumMachine.h"
#include "DrumKit.h"
#include "RandomSong.h"
#include "Theme.h"

namespace kal
{
using namespace juce;

namespace
{
// Plug-ins that measure or route rather than sound: not chosen by chance.
bool notForChance (const String& name)
{
    for (auto* w : { "analy", "meter", "tuner", "scope", "spectrum", "loudness", "test", "utility", "monitor", "midi learn", "router" })
        if (name.containsIgnoreCase (w)) return true;
    return false;
}

bool soundsLikeDrums (const String& text)
{
    for (auto* w : { "drum", "kit", "808", "909", "beat", "battery", "percuss", "kick", "snare" })
        if (text.containsIgnoreCase (w)) return true;
    return false;
}

// Devices chosen by chance that couldn't be loaded (a plug-in moved or gone since its
// scan) are taken out again, in the same undo step; how many, to say.
int dropUnloaded (Project& p, Engine& engine, const std::set<Id>& added)
{
    std::vector<Id> dead;
    for (auto id : added) if (p.device (id) && ! engine.instance (id)) dead.push_back (id);
    for (auto id : dead) edit::removeDevice (p, id);
    return (int) dead.size();
}

randomsong::Choice own (const String& name, PluginRef ref, DeviceRole role, bool wantsSample = false)
{
    randomsong::Choice c;
    c.name = name.toStdString();
    c.ref = std::move (ref);
    c.role = role;
    c.wantsSample = wantsSample;
    return c;
}
} // namespace

randomsong::Catalogue Controller::randomCatalogue (const library::Index& sounds)
{
    randomsong::Catalogue cat;
    // The scanned plug-ins, by what they do.
    for (auto& d : engine.plugins().known().getTypes())
    {
        // Kalibre's own are added below, each as it's meant to be used (the Sampler with a sound).
        if (notForChance (d.name) || d.pluginFormatName == "Kalibre") continue;
        randomsong::Choice c;
        c.name = d.name.toStdString();
        c.ref = PluginManager::refFor (d);
        c.role = engine.plugins().roleOf (d);
        c.drums = soundsLikeDrums (d.name + " " + d.category);
        (c.role == DeviceRole::Instrument ? cat.instruments : c.role == DeviceRole::MidiFx ? cat.midiEffects : cat.effects).push_back (c);
    }
    // Kalibre's own that make sound by themselves, or change it.
    cat.instruments.push_back (own ("Kalibre Synth", synthRef(), DeviceRole::Instrument));
    cat.instruments.push_back (own ("Kalibre Sampler", samplerRef(), DeviceRole::Instrument, true));
    cat.instruments.push_back (own ("Trombolese", tromboleseRef(), DeviceRole::Instrument));
    // Kalibre Drums for the drums, its pads given drum hits from the sounds (the musician's answer to the UAT).
    {
        auto kit = own ("Kalibre Drums", drumsRef(), DeviceRole::Instrument);
        kit.drums = kit.kit = true;
        cat.instruments.push_back (kit);
    }
    for (auto& s : jsfxDevices())
    {
        const String id (s.id);
        if (id == "reese-basser") cat.instruments.push_back (own (s.name, jsfxRef (s), s.role));
        if (id == "reverberator") cat.effects.push_back (own (s.name, jsfxRef (s), s.role));
        if (id == "midi-evolver") cat.midiEffects.push_back (own (s.name, jsfxRef (s), s.role));
    }
    // The musician's sounds, as the Sounds tab's scan knows them.
    for (auto& [path, it] : sounds.items)
    {
        if (! it.readable || it.kind != library::Kind::Audio || ! File (String (path)).existsAsFile()) continue;
        randomsong::Sample s;
        s.path = path;
        s.name = File (String (path)).getFileNameWithoutExtension().toStdString();
        s.seconds = it.seconds;
        s.sampleRate = it.sampleRate > 0 ? it.sampleRate : 48000;
        s.channels = it.channels > 0 ? it.channels : 2;
        s.bpm = it.tempoTrusted() ? it.bpm : 0;
        s.drums = soundsLikeDrums (String (s.name));
        s.note = it.note;
        cat.samples.push_back (s);
    }
    return cat;
}

bool Controller::randomDevices (Id trackId, unsigned seed, bool replace)
{
    const auto* t = project.track (trackId);
    if (! t) { say ("Choose a track for the random devices."); return false; }
    // Not the master: effects by chance on the whole mix are too much of a gamble.
    if (t->kind == TrackKind::Master || trackId == project.master.id)
    {
        say ("Random adds devices to tracks, not to the master.");
        return false;
    }
    static const library::Index none;
    const auto* sounds = soundsIndex ? soundsIndex() : nullptr;
    // An instrument for a track that plays notes and has none; a track of audio clips, a return,
    // a folder or the master get effects only.
    bool hasInstrument = false, hasAudio = false, hasMidi = false;
    if (! replace) for (auto& d : t->devices) hasInstrument |= d.role == DeviceRole::Instrument;
    if (t->holdsClips()) for (auto& cl : t->clips()) (cl.isMidi() ? hasMidi : hasAudio) = true;
    const bool notes = t->kind == TrackKind::Track && (hasMidi || ! hasAudio);
    const bool instrument = notes && ! hasInstrument;
    const bool drums = String (t->name).containsIgnoreCase ("drum") || String (t->name).containsIgnoreCase ("beat")
                    || String (t->name).containsIgnoreCase ("perc");
    const auto picks = randomsong::devices (randomCatalogue (sounds ? *sounds : none), seed, instrument, drums, notes && (instrument || hasInstrument));
    if (picks.empty()) { say ("There's nothing to add: scan for plug-ins in " + String (platform::inSettings ("Plug-in Manager")) + "."); return false; }
    // Each Sampler's sound loaded before it goes in.
    std::vector<std::string> states;
    for (auto& pk : picks)
    {
        std::string state;
        if (! pk.sample.empty())
        {
            KalibreSampler sampler;
            String error;
            if (sampler.load (File (String (pk.sample)), error))
            {
                MemoryBlock m;
                sampler.getStateInformation (m);
                state = m.toBase64Encoding().toStdString();
            }
        }
        states.push_back (state);
    }
    StringArray names;
    for (auto& pk : picks) names.add (String (pk.choice.name));
    if (t->isCue())
    {
        bool any = false;
        for (auto& pk : picks) any |= pk.choice.role == DeviceRole::AudioFx;
        if (! any) return false;
    }
    std::set<Id> added;
    apply ("Random Devices", [&] (Project& p)
    {
        // A click starts the track again: what was on it goes, in this same undo step.
        if (replace)
            if (auto* tr = p.track (trackId))
            {
                std::vector<Id> ids;
                for (auto& d : tr->devices) ids.push_back (d.id);
                for (auto id : ids) edit::removeDevice (p, id);
            }
        for (size_t i = 0; i < picks.size(); ++i)
        {
            const auto& pk = picks[i];
            if (p.track (trackId) && p.track (trackId)->isCue() && pk.choice.role != DeviceRole::AudioFx) continue;
            const Id d = edit::addDevice (p, trackId, pk.choice.ref, pk.choice.role);
            added.insert (d);
            if (auto* dev = p.device (d); dev && ! states[i].empty()) dev->state = states[i];
        }
    });
    StringArray kept;
    for (auto id : added) if (auto* d = project.device (id); d && engine.instance (id)) kept.add (String (d->plugin.name));
    // Taken out within the same undo step: Undo still goes back to before the click.
    const int gone = dropUnloaded (project, engine, added);
    if (gone > 0) syncEngine();
    zoneTab = ZoneTab::Devices;
    say ("Random devices on " + String (project.track (trackId)->name) + ": " + (kept.isEmpty() ? String ("none") : kept.joinIntoString (", "))
         + (gone > 0 ? " (" + String (gone) + (gone == 1 ? " plug-in couldn't" : " plug-ins couldn't") + " be loaded, and " + (gone == 1 ? "was" : "were") + " left out)." : String (".")),
         gone > 0, true);
    sendChangeMessage();
    return true;
}

bool Controller::randomSong (const library::Index& sounds, unsigned seed, bool addTo)
{
    randomsong::Options o;
    o.seed = seed;
    if (addTo)
    {
        o.intoSong = true;
        o.bpm = project.tempo.bpmAt (0);
        o.scale = project.scale;
        o.firstId = project.nextId;
    }
    auto made = randomsong::make (randomCatalogue (sounds), o);
    auto& p = made.project;
    if (p.tracks.empty()) { say ("A random song couldn't be made: the music engines didn't answer.", true); return false; }

    // Audio Kalibre keeps is 32-bit float WAV (decision 0079): loops that aren't are copied as one.
    for (auto& t : p.tracks)
        for (auto& c : t.clips())
            if (! c.isMidi())
            {
                const File f (String (c.audio.file));
                if (audiofile::isFloatWav (f)) continue;
                const auto copy = recordingsFolder().getNonexistentChildFile (f.getFileNameWithoutExtension(), ".wav");
                if (audiofile::convertToFloatWav (f, copy)) c.audio.file = copy.getFullPathName().toStdString();
            }
    // Each Sampler with its sound loaded, saved in its state as the browser's New Track does.
    for (auto& [device, path] : made.samplerSamples)
        if (auto* d = p.device (device))
        {
            KalibreSampler sampler;
            String error;
            if (! sampler.load (File (String (path)), error)) continue;
            MemoryBlock state;
            sampler.getStateInformation (state);
            d->state = state.toBase64Encoding().toStdString();
        }
    // Each Kalibre Drums with a drum hit of its kind on each pad; the drums clip plays it, not its own steps.
    for (auto id : made.drumKits)
        if (auto* d = p.device (id))
        {
            DrumMachine kit (engine.plugins());
            auto k = drumkit::standard();
            k.playsPattern = false;
            const auto chosen = drumkit::randomSamples (k, sounds, seed);
            for (size_t i = 0; i < k.pads.size() && i < chosen.size(); ++i)
                if (! chosen[i].empty()) k.pads[i].sample = chosen[i];
            kit.setKit (k);
            MemoryBlock state;
            kit.getStateInformation (state);
            d->state = state.toBase64Encoding().toStdString();
        }
    // The house colours, a track each.
    auto& colours = theme::trackColours();
    const size_t had = addTo ? project.tracks.size() : 0;
    for (size_t i = 0; i < p.tracks.size(); ++i) p.tracks[i].colour = colours[(int) (had + i + 1) % colours.size()].getARGB();

    // Shift-click: its tracks join this song, after the ones there are, one undo step.
    if (addTo)
    {
        std::set<Id> added;
        for (auto& t : p.tracks) for (auto& d : t.devices) added.insert (d.id);
        // Never the name of a track the song has already: "Drums 2" beside its Drums (the UAT).
        std::set<std::string> taken;
        for (auto& t : project.tracks) taken.insert (t.name);
        for (auto& t : p.tracks)
        {
            std::string name = t.name;
            for (int n = 2; taken.count (name); ++n) name = t.name + " " + std::to_string (n);
            t.name = name;
            taken.insert (name);
        }
        const Id first = p.tracks.front().id;
        const bool ok = apply ("Random Tracks", [&] (Project& song)
        {
            for (auto& t : p.tracks) song.tracks.push_back (t);
            song.nextId = std::max (song.nextId, p.nextId);
        });
        if (! ok) return false;
        const int gone = dropUnloaded (project, engine, added);
        if (gone > 0) syncEngine();
        focusTrack = first;
        selectedTracks = { focusTrack };
        sendChangeMessage();
        say ("Random tracks added: " + String (made.summary) + "." + (gone > 0 ? " (" + String (gone) + (gone == 1 ? " plug-in couldn't be loaded and was left out.)" : " plug-ins couldn't be loaded and were left out.)") : String()),
             false, true);
        lastRandomParts = made.parts;
        return true;
    }

    // A new song, as File > New is: the one before was saved or let go of first.
    engine.stop();
    engine.newSong();
    project = std::move (p);
    beginWorkClock();
    history.clear();
    // The activity log says it began by chance, with what was made (0135).
    activityLog.clear();
    logActivity (activity::Kind::New, "A new random song (the Random button), in Kalibre " + String (KALIBRE_VERSION) + ": " + String (made.summary));
    file = File();
    dirty = true;
    focusTrack = project.tracks.front().id;
    selectedTracks = { focusTrack };
    selectedClip = 0;
    selectedNotes.clear();
    cursor = 0;
    syncEngine();
    std::set<Id> every;
    for (auto& t : project.tracks) for (auto& d : t.devices) every.insert (d.id);
    const int gone = dropUnloaded (project, engine, every);
    if (gone > 0) syncEngine();
    sendChangeMessage();
    say ("A random song: " + String (made.summary) + ". Press Space to hear it; click Random again for another."
         + (gone > 0 ? " (" + String (gone) + (gone == 1 ? " plug-in couldn't be loaded and was left out.)" : " plug-ins couldn't be loaded and were left out.)") : String()));
    lastRandomParts = made.parts;
    return true;
}

} // namespace kal
