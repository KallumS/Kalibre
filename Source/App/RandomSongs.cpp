// The Random button's song (decision 0101): what Kalibre has to choose from,
// the song made by Core's RandomSong, its sounds made ready, put in its place.
#include "Controller.h"

#include "AudioFiles.h"
#include "BuiltinInstruments.h"
#include "Edit.h"
#include "JsfxDevice.h"
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

bool Controller::randomSong (const library::Index& sounds, unsigned seed)
{
    randomsong::Options o;
    o.seed = seed;
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
    // The house colours, a track each.
    auto& colours = theme::trackColours();
    for (size_t i = 0; i < p.tracks.size(); ++i) p.tracks[i].colour = colours[(int) (i + 1) % colours.size()].getARGB();

    // A new song, as File > New is: the one before was saved or let go of first.
    engine.stop();
    engine.newSong();
    project = std::move (p);
    beginWorkClock();
    history.clear();
    file = File();
    dirty = true;
    focusTrack = project.tracks.front().id;
    selectedTracks = { focusTrack };
    selectedClip = 0;
    selectedNotes.clear();
    cursor = 0;
    syncEngine();
    sendChangeMessage();
    say ("A random song: " + String (made.summary) + ". Press Space to hear it; click Random again for another.");
    lastRandomParts = made.parts;
    return true;
}

} // namespace kal
