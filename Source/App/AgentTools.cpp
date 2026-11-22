#include "AgentTools.h"
#include "DrumMachine.h"

#include "BuiltinInstruments.h"
#include "ContainerProcessor.h"
#include "MultibandProcessor.h"
#include "StereoSplitProcessor.h"
#include "Edit.h"
#include "HardwareInsert.h"
#include "JsfxDevice.h"
#include "LuaTool/LuaTool.h"
#include "MorphProcessor.h"
#include "NoteSplitter.h"

#include <cmath>

namespace kal
{
using namespace juce;
using json = mcp::json;
using mcp::Reply;

// A parameter's value as it reads, its unit once: Kalibre's own put it in their text already
// (the UAT read "4200 Pa  Pa").
juce::String readableValue (juce::AudioProcessorParameter& p)
{
    auto text = p.getText (p.getValue(), 32).trim();
    const auto unit = p.getLabel().trim();
    if (unit.isNotEmpty() && ! text.endsWith (unit)) text << " " << unit;
    return text;
}

namespace
{
double beatsOf (Tick t) { return (double) t / PPQ; }
Tick ticksOf (double beats) { return (Tick) std::llround (beats * PPQ); }

const char* kindName (TrackKind k)
{
    switch (k)
    {
        case TrackKind::Folder: return "folder";
        case TrackKind::Return: return "return";
        case TrackKind::Master: return "master";
        case TrackKind::Summing: return "summing";
        case TrackKind::Cue: return "cue";
        default: return "track";
    }
}

const char* roleName (DeviceRole r)
{
    return r == DeviceRole::MidiFx ? "midi effect" : r == DeviceRole::Instrument ? "instrument" : "audio effect";
}

// A track by id (a number) or by name (any case); the chosen track when none is given.
const Track* findTrack (Controller& c, const json& args, const char* key = "track")
{
    if (! args.contains (key) || args[key].is_null()) return c.project.track (c.focusTrack);
    const auto& v = args[key];
    if (v.is_number_integer()) return c.project.track ((Id) v.get<long long>());
    if (v.is_string())
    {
        const String name (v.get<std::string>());
        if (name.containsOnly ("0123456789") && name.isNotEmpty())
            if (auto* t = c.project.track ((Id) name.getLargeIntValue())) return t;
        for (auto& t : c.project.tracks) if (String (t.name).equalsIgnoreCase (name)) return &t;
        if (name.equalsIgnoreCase ("master")) return &c.project.master;
    }
    return nullptr;
}

Id idOf (const json& args, const char* key)
{
    if (! args.contains (key)) return 0;
    const auto& v = args[key];
    if (v.is_number_integer()) return (Id) v.get<long long>();
    if (v.is_string()) return (Id) String (v.get<std::string>()).getLargeIntValue();
    return 0;
}

double numberOr (const json& args, const char* key, double fallback)
{
    return args.contains (key) && args[key].is_number() ? args[key].get<double>() : fallback;
}

std::string textOr (const json& args, const char* key, const std::string& fallback = {})
{
    return args.contains (key) && args[key].is_string() ? args[key].get<std::string>() : fallback;
}

bool flagOr (const json& args, const char* key, bool fallback)
{
    return args.contains (key) && args[key].is_boolean() ? args[key].get<bool>() : fallback;
}

// Every device that can be added: the plug-ins scanned, and Kalibre's own.
struct Addable
{
    String name, format, maker;
    DeviceRole role = DeviceRole::AudioFx;
    std::optional<PluginDescription> plugin;   // a scanned plug-in
    PluginRef builtIn;                         // otherwise one of Kalibre's own
};

std::vector<Addable> addable (Controller& c)
{
    std::vector<Addable> out;
    for (auto& d : c.engine.plugins().known().getTypes())
        out.push_back ({ d.name, d.pluginFormatName, d.manufacturerName, c.engine.plugins().roleOf (d), d, {} });
    auto own = [&out] (const String& name, PluginRef ref, DeviceRole role) { out.push_back ({ name, "Kalibre", "Kalibre", role, std::nullopt, ref }); };
    own ("Kalibre Synth", synthRef(), DeviceRole::Instrument);
    own ("Kalibre Sampler", samplerRef(), DeviceRole::Instrument);
    own ("Kalibre Drums", drumsRef(), DeviceRole::Instrument);
    own ("Trombolese", tromboleseRef(), DeviceRole::Instrument);
    own ("Hardware Insert", hardwareInsertRef(), DeviceRole::AudioFx);
    own ("X/Y Morph", morphRef(), DeviceRole::Instrument);
    own ("Container", containerRef(), DeviceRole::AudioFx);
    own ("Multiband", multibandRef(), DeviceRole::AudioFx);
    own ("Stereo Split", stereoSplitRef(), DeviceRole::AudioFx);
    own ("Note Splitter", noteSplitterRef(), DeviceRole::MidiFx);
    for (auto& s : jsfxDevices()) own (s.name, jsfxRef (s), s.role);
    own ("Instant Virtual Instrument", instantVirtualInstrumentRef(), DeviceRole::Instrument);
    own ("ScaleView", scaleViewRef(), DeviceRole::MidiFx);
    for (auto& s : luaTools()) own (s.name, luaToolRef (s), DeviceRole::MidiFx);
    return out;
}

// By name: exactly (any case) first, then the first whose name holds it.
const Addable* findAddable (const std::vector<Addable>& all, const String& name)
{
    for (auto& a : all) if (a.name.equalsIgnoreCase (name)) return &a;
    for (auto& a : all) if (a.name.containsIgnoreCase (name)) return &a;
    return nullptr;
}

json deviceJson (Controller& c, const Device& d)
{
    json j = { { "id", d.id }, { "name", d.plugin.name }, { "kind", roleName (d.role) }, { "on", d.active }, { "bypassed", d.bypassed } };
    if (auto* inst = c.engine.instance (d.id)) j["parameters"] = inst->getParameters().size();
    return j;
}

json clipJson (const Clip& cl, const TempoMap& tempo)
{
    json j = { { "id", cl.id }, { "kind", cl.isMidi() ? "midi" : "audio" }, { "name", cl.name },
               { "start_beat", beatsOf (cl.start) }, { "length_beats", beatsOf (cl.length) },
               { "starts_at", tempo.formatBarBeat (cl.start) }, { "muted", cl.muted } };
    if (cl.isMidi()) j["notes"] = cl.midi.notes.size();
    else j["file"] = cl.audio.file;
    return j;
}

json trackJson (Controller& c, const Track& t)
{
    json j = { { "id", t.id }, { "name", t.name }, { "kind", kindName (t.kind) }, { "volume_db", t.volumeDb }, { "pan", t.pan },
               { "mute", t.mute }, { "solo", t.solo }, { "armed", t.armed } };
    if (t.parent) j["folder"] = t.parent;
    json devices = json::array();
    for (auto& d : t.devices) devices.push_back (deviceJson (c, d));
    j["devices"] = devices;
    if (t.holdsClips())
    {
        json clips = json::array();
        for (auto& cl : t.clips()) clips.push_back (clipJson (cl, c.project.tempo));
        j["clips"] = clips;
    }
    return j;
}

Reply noTrack() { return Reply::fail ("No such track. get_song lists every track's id and name."); }
Reply noClip() { return Reply::fail ("No such clip. get_song lists every clip's id."); }
Reply noDevice() { return Reply::fail ("No such device. get_song lists every track's devices with their ids."); }

// Changes the song as one undo step, and says so in the message line.
bool change (Controller& c, const String& what, std::function<void (Project&)> f)
{
    const bool ok = c.apply ("Agent: " + what, std::move (f));
    if (ok) c.say ("An AI agent: " + what + ".", false, true);
    return ok;
}
} // namespace

//==============================================================================
void addAgentTools (mcp::Server& server, Controller& c)
{
    using namespace mcp;
    auto tool = [&server] (std::string name, std::string description, json schema, std::function<Reply (const json&)> run, bool readOnly = false)
    {
        Tool t;
        t.name = std::move (name);
        t.description = std::move (description);
        t.inputSchema = std::move (schema);
        t.run = std::move (run);
        t.readOnly = readOnly;
        server.add (std::move (t));
    };

    //--------------------------------------------------------------------------
    // Looking
    tool ("get_song", "The song as it is now: its tempo, time signature, scale, loop and cursor, and every track with its id, kind, "
                      "volume, devices (plug-ins, with ids) and clips (with ids, positions in beats). Read this first.",
          object ({}), [&c] (const json&)
    {
        const auto& p = c.project;
        const auto& m = p.tempo.meterAt (c.cursor);
        json song = { { "file", c.file.getFullPathName().toStdString() }, { "tempo_bpm", p.tempo.bpmAt (c.cursor) },
                      { "time_signature", std::to_string (m.numerator) + "/" + std::to_string (m.denominator) },
                      { "cursor_beat", beatsOf (c.cursor) }, { "playing", c.engine.isPlaying() },
                      { "loop", { { "on", p.loop.on }, { "start_beat", beatsOf (p.loop.start) }, { "end_beat", beatsOf (p.loop.end) } } },
                      { "chosen_track", c.focusTrack },
                      { "song_version", p.songVersion + 1 }, { "song_versions", p.songVersions() } };
        json tracks = json::array();
        for (auto& t : p.tracks) tracks.push_back (trackJson (c, t));
        song["tracks"] = tracks;
        song["master"] = trackJson (c, p.master);
        return Reply::ok (song.dump (1));
    }, true);

    tool ("list_plugins", "The plug-ins and Kalibre's own devices that can be added, with their kind (MIDI effect, instrument or audio "
                          "effect). `search` keeps those whose name, maker or format holds it.",
          object ({ { "search", text ("Part of a name, maker or format, e.g. \"Pro-Q\" or \"Kalibre\".") } }), [&c] (const json& a)
    {
        const String search (textOr (a, "search"));
        json out = json::array();
        for (auto& d : addable (c))
            if (search.isEmpty() || d.name.containsIgnoreCase (search) || d.maker.containsIgnoreCase (search) || d.format.containsIgnoreCase (search))
                out.push_back ({ { "name", d.name.toStdString() }, { "maker", d.maker.toStdString() }, { "format", d.format.toStdString() },
                                 { "kind", roleName (d.role) } });
        return Reply::ok (out.dump (1));
    }, true);

    //--------------------------------------------------------------------------
    // Tracks
    tool ("create_track", "Adds a track (after the chosen one). Optionally with an instrument on it, by name (see list_plugins).",
          object ({ { "name", text ("Its name.") }, { "kind", oneOf ("What kind of track.", { "track", "folder", "return" }) },
                    { "instrument", text ("An instrument to put on it, e.g. \"Kalibre Synth\".") } }), [&c] (const json& a)
    {
        const auto kindText = textOr (a, "kind", "track");
        const TrackKind kind = kindText == "folder" ? TrackKind::Folder : kindText == "return" ? TrackKind::Return : TrackKind::Track;
        const String name (textOr (a, "name"));
        const Addable* instrument = nullptr;
        const auto all = addable (c);
        if (auto want = textOr (a, "instrument"); ! want.empty())
            if (! (instrument = findAddable (all, want))) return Reply::fail ("No plug-in is called " + want + ". list_plugins lists them.");
        const Id made = c.addTrack (kind, name);
        if (! made) return Reply::fail ("The track couldn't be made (Kalibre holds 1000 at most).");
        if (instrument)
        {
            if (instrument->plugin) c.addPlugin (made, *instrument->plugin, instrument->role);
            else c.addBuiltIn (made, instrument->builtIn, instrument->role);
        }
        c.say ("An AI agent made the track " + String (c.project.track (made)->name) + ".", false, true);
        return Reply::ok (trackJson (c, *c.project.track (made)).dump (1));
    });

    tool ("update_track", "Changes a track: its name, volume (dB), pan (-1 left to 1 right), mute, solo, or arms it for recording.",
          object ({ { "track", idOrName ("The track, by id or name.") }, { "name", text ("A new name.") },
                    { "volume_db", number ("Its fader, -60 to +6 dB.") }, { "pan", number ("-1 (left) to 1 (right).") },
                    { "mute", flag ("Muted.") }, { "solo", flag ("Soloed.") }, { "armed", flag ("Armed to record.") } }, { "track" }),
          [&c] (const json& a)
    {
        const auto* t = findTrack (c, a);
        if (! t) return noTrack();
        const Id id = t->id;
        change (c, "Change " + String (t->name), [&] (Project& p)
        {
            auto* x = p.track (id);
            if (! x) x = p.master.id == id ? &p.master : nullptr;
            if (! x) return;
            if (a.contains ("name") && a["name"].is_string()) x->name = a["name"];
            if (a.contains ("volume_db") && a["volume_db"].is_number()) x->volumeDb = jlimit (-60.0, 6.0, a["volume_db"].get<double>());
            if (a.contains ("pan") && a["pan"].is_number()) x->pan = jlimit (-1.0, 1.0, a["pan"].get<double>());
            x->mute = flagOr (a, "mute", x->mute);
            x->solo = flagOr (a, "solo", x->solo);
            x->armed = flagOr (a, "armed", x->armed);
        });
        const auto* now = c.project.track (id);
        return Reply::ok (trackJson (c, now ? *now : c.project.master).dump (1));
    });

    tool ("delete_track", "Deletes a track, its clips and plug-ins (a folder's tracks with it). One undo step.",
          object ({ { "track", idOrName ("The track, by id or name.") } }, { "track" }), [&c] (const json& a)
    {
        const auto* t = findTrack (c, a);
        if (! t || t->kind == TrackKind::Master) return noTrack();
        const Id id = t->id;
        const String name (t->name);
        change (c, "Delete " + name, [id] (Project& p) { edit::removeTrack (p, id); });
        return Reply::ok ("Deleted " + name.toStdString() + ".");
    });

    //--------------------------------------------------------------------------
    // Plug-ins
    tool ("add_plugin", "Adds a plug-in or one of Kalibre's own devices to a track, by name (list_plugins lists them). MIDI effects, "
                        "instruments and audio effects keep that order on a track; `position` places it among its kind.",
          object ({ { "track", idOrName ("The track, by id or name. The chosen track when left out.") },
                    { "plugin", text ("Its name, e.g. \"Kalibre Sampler\" or \"Pro-Q 3\".") },
                    { "position", integer ("Where in the track's devices, from 0. Last of its kind when left out.") } }, { "plugin" }),
          [&c] (const json& a)
    {
        const auto* t = findTrack (c, a);
        if (! t) return noTrack();
        const auto all = addable (c);
        const auto want = textOr (a, "plugin");
        const auto* d = findAddable (all, want);
        if (! d) return Reply::fail ("No plug-in is called " + want + ". list_plugins lists them.");
        const Id track = t->id;
        std::set<Id> before;
        for (auto& x : t->devices) before.insert (x.id);
        const int position = a.contains ("position") && a["position"].is_number_integer() ? a["position"].get<int>() : -1;
        if (d->plugin) c.addPlugin (track, *d->plugin, d->role, position);
        else change (c, "Add " + d->name, [&] (Project& p) { edit::addDevice (p, track, d->builtIn, d->role, position); });
        for (auto& x : c.project.track (track)->devices)
            if (! before.count (x.id))
            {
                c.say ("An AI agent added " + d->name + " to " + String (c.project.track (track)->name) + ".", false, true);
                json j = deviceJson (c, x);
                if (! c.engine.instance (x.id)) j["warning"] = "It couldn't be loaded.";
                return Reply::ok (j.dump (1));
            }
        return Reply::fail ("It couldn't be added to that track (the Cue track takes audio effects only).");
    });

    tool ("remove_plugin", "Takes a device off its track.", object ({ { "device", integer ("The device's id.") } }, { "device" }), [&c] (const json& a)
    {
        const Id id = idOf (a, "device");
        const auto* d = c.project.device (id);
        if (! d) return noDevice();
        const String name (d->plugin.name);
        change (c, "Remove " + name, [id] (Project& p) { edit::removeDevice (p, id); });
        return Reply::ok ("Removed " + name.toStdString() + ".");
    });

    tool ("bypass_plugin", "Bypasses a device (its sound passes through unchanged), or brings it back.",
          object ({ { "device", integer ("The device's id.") }, { "bypassed", flag ("True to bypass, false to bring it back.") } }, { "device", "bypassed" }),
          [&c] (const json& a)
    {
        const Id id = idOf (a, "device");
        const auto* d = c.project.device (id);
        if (! d) return noDevice();
        const bool on = flagOr (a, "bypassed", true);
        change (c, String (on ? "Bypass " : "Un-bypass ") + d->plugin.name, [id, on] (Project& p) { if (auto* x = p.device (id)) x->bypassed = on; });
        return Reply::ok (on ? "Bypassed." : "Playing again.");
    });
    tool ("randomise_plugin", "Sets a device's controls by chance, as its Random button does (decision 0104): every control but those "
                              "for volume, tuning and pitch (and bypass, mute and meters). One undo step.",
          object ({ { "device", integer ("The device's id.") } }, { "device" }), [&c] (const json& a)
    {
        const Id id = idOf (a, "device");
        if (! c.project.device (id)) return noDevice();
        const int changed = c.randomSettings (id);
        if (changed == 0) return Reply::fail (c.message.toStdString());
        return Reply::ok (c.message.toStdString());
    });

    tool ("get_plugin_parameters", "A device's parameters: index, name, value (0 to 1) and what it reads as. `search` keeps those whose name holds it.",
          object ({ { "device", integer ("The device's id.") }, { "search", text ("Part of a parameter's name, e.g. \"cutoff\".") } }, { "device" }),
          [&c] (const json& a)
    {
        auto* inst = c.engine.instance (idOf (a, "device"));
        if (! inst) return noDevice();
        const String search (textOr (a, "search"));
        json out = json::array();
        const auto& params = inst->getParameters();
        for (int i = 0; i < params.size(); ++i)
        {
            auto* p = params[i];
            const auto name = p->getName (64);
            if (search.isNotEmpty() && ! name.containsIgnoreCase (search)) continue;
            out.push_back ({ { "index", i }, { "name", name.toStdString() }, { "value", p->getValue() },
                             { "text", readableValue (*p).toStdString() } });
            if (out.size() >= 400) break;
        }
        return Reply::ok (out.dump (1));
    }, true);

    tool ("set_plugin_parameter", "Sets a device's parameter, by index or name, to a value from 0 to 1 (get_plugin_parameters shows "
                                  "what each reads as). Like turning the knob: not an undo step of its own.",
          object ({ { "device", integer ("The device's id.") }, { "parameter", idOrName ("Its index, or its name.") },
                    { "value", number ("0 to 1.") } }, { "device", "parameter", "value" }), [&c] (const json& a)
    {
        auto* inst = c.engine.instance (idOf (a, "device"));
        if (! inst) return noDevice();
        const auto& params = inst->getParameters();
        AudioProcessorParameter* p = nullptr;
        if (a["parameter"].is_number_integer()) p = params[a["parameter"].get<int>()];
        else if (a["parameter"].is_string())
        {
            const String name (a["parameter"].get<std::string>());
            for (auto* x : params) if (x->getName (64).equalsIgnoreCase (name)) { p = x; break; }
            if (! p) for (auto* x : params) if (x->getName (64).containsIgnoreCase (name)) { p = x; break; }
        }
        if (! p) return Reply::fail ("No such parameter. get_plugin_parameters lists them.");
        p->setValueNotifyingHost ((float) jlimit (0.0, 1.0, numberOr (a, "value", p->getValue())));
        c.say ("An AI agent set " + p->getName (64) + " to " + readableValue (*p) + ".");
        return Reply::ok (p->getName (64).toStdString() + " is now " + readableValue (*p).toStdString() + ".");
    });

    //--------------------------------------------------------------------------
    // Clips and notes
    tool ("create_midi_clip", "Makes an empty MIDI clip on a track. Positions are in beats (quarter notes) from the song's start: "
                              "bar 2 of 4/4 starts at beat 4.",
          object ({ { "track", idOrName ("The track, by id or name. The chosen track when left out.") },
                    { "start_beat", number ("Where it starts, in beats from the song's start.") },
                    { "length_beats", number ("How long, in beats (4 is a bar of 4/4).") }, { "name", text ("Its name.") } },
                  { "start_beat", "length_beats" }), [&c] (const json& a)
    {
        const auto* t = findTrack (c, a);
        if (! t || ! t->holdsClips()) return noTrack();
        const Id track = t->id;
        const Tick start = std::max<Tick> (0, ticksOf (numberOr (a, "start_beat", 0))), length = std::max<Tick> (1, ticksOf (numberOr (a, "length_beats", 4)));
        Id made = 0;
        const auto name = textOr (a, "name");
        change (c, "New MIDI Clip", [&] (Project& p)
        {
            made = edit::addMidiClip (p, track, start, length);
            if (auto* cl = p.clip (made); cl && ! name.empty()) cl->name = name;
        });
        if (! made) return Reply::fail ("The clip couldn't be made.");
        return Reply::ok (clipJson (*c.project.clip (made), c.project.tempo).dump (1));
    });

    tool ("get_clip", "A clip in detail: where it is, and a MIDI clip's notes (id, pitch, start and length in beats from the clip's "
                      "start, velocity). Pitches are MIDI numbers: 60 is middle C.",
          object ({ { "clip", integer ("The clip's id.") } }, { "clip" }), [&c] (const json& a)
    {
        const auto* cl = c.project.clip (idOf (a, "clip"));
        if (! cl) return noClip();
        json j = clipJson (*cl, c.project.tempo);
        if (cl->isMidi())
        {
            json notes = json::array();
            for (auto& n : cl->midi.notes)
                notes.push_back ({ { "id", n.id }, { "pitch", n.pitch }, { "start_beat", beatsOf (n.start - cl->offset) },
                                   { "length_beats", beatsOf (n.length) }, { "velocity", n.velocity } });
            j["notes"] = notes;
        }
        return Reply::ok (j.dump (1));
    }, true);

    tool ("add_notes", "Writes notes into a MIDI clip, as one undo step. Each note: pitch (MIDI number, 60 is middle C), start_beat "
                       "(from the clip's start), length_beats, and velocity (1-127, 100 when left out).",
          object ({ { "clip", integer ("The clip's id.") },
                    { "notes", { { "type", "array" }, { "description", "The notes to add." },
                                 { "items", object ({ { "pitch", integer ("MIDI note number, 0-127.") }, { "start_beat", number ("Beats from the clip's start.") },
                                                      { "length_beats", number ("How long, in beats.") }, { "velocity", integer ("1-127.") } },
                                                    { "pitch", "start_beat", "length_beats" }) } } } }, { "clip", "notes" }),
          [&c] (const json& a)
    {
        const Id clip = idOf (a, "clip");
        const auto* cl = c.project.clip (clip);
        if (! cl || ! cl->isMidi()) return Reply::fail ("No such MIDI clip. get_song lists the clips.");
        if (! a.contains ("notes") || ! a["notes"].is_array()) return Reply::fail ("Give the notes as a list.");
        const Tick offset = cl->offset;
        std::vector<Note> notes;
        for (auto& n : a["notes"])
        {
            Note x;
            x.pitch = jlimit (0, 127, (int) numberOr (n, "pitch", 60));
            x.start = std::max<Tick> (0, ticksOf (numberOr (n, "start_beat", 0)) + offset);
            x.length = std::max<Tick> (1, ticksOf (numberOr (n, "length_beats", 1)));
            x.velocity = jlimit (1, 127, (int) numberOr (n, "velocity", 100));
            notes.push_back (x);
        }
        std::vector<Id> ids;
        change (c, "Add " + String ((int) notes.size()) + (notes.size() == 1 ? " Note" : " Notes"), [&] (Project& p)
        {
            for (auto& n : notes) ids.push_back (edit::addNote (p, clip, n));
        });
        return Reply::ok ("Added " + std::to_string (ids.size()) + " notes: ids " + json (ids).dump() + ".");
    });

    tool ("remove_notes", "Removes notes from a MIDI clip: those listed by id, or every note when `all` is true.",
          object ({ { "clip", integer ("The clip's id.") }, { "note_ids", { { "type", "array" }, { "items", { { "type", "integer" } } }, { "description", "The notes' ids." } } },
                    { "all", flag ("Every note in the clip.") } }, { "clip" }), [&c] (const json& a)
    {
        const Id clip = idOf (a, "clip");
        const auto* cl = c.project.clip (clip);
        if (! cl || ! cl->isMidi()) return Reply::fail ("No such MIDI clip.");
        std::set<Id> ids;
        if (flagOr (a, "all", false)) for (auto& n : cl->midi.notes) ids.insert (n.id);
        else if (a.contains ("note_ids") && a["note_ids"].is_array()) for (auto& v : a["note_ids"]) if (v.is_number_integer()) ids.insert ((Id) v.get<long long>());
        if (ids.empty()) return Reply::fail ("No notes named: give note_ids, or all.");
        change (c, "Remove Notes", [&] (Project& p) { edit::removeNotes (p, clip, ids); });
        return Reply::ok ("Removed " + std::to_string (ids.size()) + " notes.");
    });

    tool ("edit_clip", "Moves, trims, renames or mutes a clip, or moves it to another track. Positions in beats from the song's start.",
          object ({ { "clip", integer ("The clip's id.") }, { "start_beat", number ("Move it to start here.") },
                    { "length_beats", number ("Make it this long (trimming its end).") }, { "track", idOrName ("Move it to this track.") },
                    { "name", text ("A new name.") }, { "muted", flag ("Muted.") } }, { "clip" }), [&c] (const json& a)
    {
        const Id clip = idOf (a, "clip");
        const auto* cl = c.project.clip (clip);
        if (! cl) return noClip();
        Id toTrack = 0;
        if (a.contains ("track")) { const auto* t = findTrack (c, a); if (! t || ! t->holdsClips()) return noTrack(); toTrack = t->id; }
        change (c, "Edit Clip", [&] (Project& p)
        {
            auto* x = p.clip (clip);
            if (! x) return;
            if (a.contains ("length_beats") && a["length_beats"].is_number())
                edit::resizeClip (p, clip, x->start, x->start + std::max<Tick> (1, ticksOf (a["length_beats"].get<double>())));
            if (a.contains ("start_beat") || toTrack)
                edit::moveClip (p, clip, a.contains ("start_beat") ? std::max<Tick> (0, ticksOf (numberOr (a, "start_beat", 0))) : p.clip (clip)->start, toTrack);
            if (auto* y = p.clip (clip))
            {
                if (a.contains ("name") && a["name"].is_string()) y->name = a["name"];
                y->muted = flagOr (a, "muted", y->muted);
            }
        });
        return Reply::ok (clipJson (*c.project.clip (clip), c.project.tempo).dump (1));
    });

    tool ("delete_clip", "Deletes a clip.", object ({ { "clip", integer ("The clip's id.") } }, { "clip" }), [&c] (const json& a)
    {
        const Id clip = idOf (a, "clip");
        if (! c.project.clip (clip)) return noClip();
        change (c, "Delete Clip", [clip] (Project& p) { edit::removeClips (p, { clip }); });
        return Reply::ok ("Deleted.");
    });

    tool ("duplicate_clip", "Copies a clip to just after itself; returns the copy.", object ({ { "clip", integer ("The clip's id.") } }, { "clip" }),
          [&c] (const json& a)
    {
        const Id clip = idOf (a, "clip");
        if (! c.project.clip (clip)) return noClip();
        Id made = 0;
        change (c, "Duplicate Clip", [&] (Project& p) { made = edit::duplicateClip (p, clip); });
        if (! made) return Reply::fail ("It couldn't be duplicated.");
        return Reply::ok (clipJson (*c.project.clip (made), c.project.tempo).dump (1));
    });

    tool ("clip_action", "Does something to a clip: reverse (backwards), invert (MIDI upside down; audio's polarity), "
                         "split_at_transients (audio: a clip for each hit), quantise (MIDI notes onto a grid of `grid_beats`), "
                         "transpose (MIDI notes by `semitones`), or split (at `beat`, from the song's start).",
          object ({ { "clip", integer ("The clip's id.") },
                    { "action", oneOf ("What to do.", { "reverse", "invert", "split_at_transients", "quantise", "transpose", "split" }) },
                    { "grid_beats", number ("quantise: the grid, in beats (0.25 is a sixteenth).") },
                    { "semitones", integer ("transpose: how far, up or down.") }, { "beat", number ("split: where, in beats from the song's start.") } },
                  { "clip", "action" }), [&c] (const json& a)
    {
        const Id clip = idOf (a, "clip");
        const auto* cl = c.project.clip (clip);
        if (! cl) return noClip();
        const auto action = textOr (a, "action");
        std::set<Id> all;
        for (auto& n : cl->midi.notes) all.insert (n.id);
        if (action == "reverse") change (c, "Reverse", [clip] (Project& p) { edit::reverseClip (p, clip); });
        else if (action == "invert") change (c, "Invert", [clip] (Project& p) { edit::invertClip (p, clip); });
        else if (action == "split_at_transients")
        {
            if (cl->isMidi()) return Reply::fail ("Split at transients is for audio clips.");
            const int made = c.splitAtTransients (clip);
            return made > 1 ? Reply::ok ("Split into " + std::to_string (made) + " clips.") : Reply::fail ("No hits were found to split at.");
        }
        else if (action == "quantise")
        {
            if (! cl->isMidi()) return Reply::fail ("Quantise is for MIDI clips here.");
            const Tick grid = std::max<Tick> (1, ticksOf (numberOr (a, "grid_beats", 0.25)));
            change (c, "Quantise", [&] (Project& p) { edit::quantise (p, clip, all, grid); });
        }
        else if (action == "transpose")
        {
            if (! cl->isMidi()) return Reply::fail ("Transpose is for MIDI clips here.");
            const int by = (int) numberOr (a, "semitones", 0);
            change (c, "Transpose", [&] (Project& p) { edit::transpose (p, clip, all, by); });
        }
        else if (action == "split")
        {
            const Tick at = ticksOf (numberOr (a, "beat", -1));
            if (at <= cl->start || at >= cl->end()) return Reply::fail ("Split needs a beat inside the clip.");
            Id right = 0;
            change (c, "Split", [&] (Project& p) { right = edit::splitClip (p, clip, at); });
            return Reply::ok ("Split; the right half is clip " + std::to_string (right) + ".");
        }
        else return Reply::fail ("Unknown action " + action + ".");
        return Reply::ok ("Done.");
    });

    tool ("import_audio", "Brings an audio file (WAV, AIFF, FLAC, MP3, Ogg; on the Mac AAC and ALAC too) or a MIDI file onto a track at a beat. "
                          "With split_at_transients, an audio file comes in as a clip for each hit.",
          object ({ { "file", text ("The file's full path.") }, { "track", idOrName ("The track; a new one when left out and none is chosen.") },
                    { "start_beat", number ("Where, in beats from the song's start. The cursor when left out.") },
                    { "split_at_transients", flag ("A clip for each hit (audio).") } }, { "file" }), [&c] (const json& a)
    {
        const File f (String (textOr (a, "file")));
        if (! f.existsAsFile()) return Reply::fail ("There's no file at " + f.getFullPathName().toStdString() + ".");
        const auto* t = findTrack (c, a);
        const Id track = t ? t->id : 0;
        const Tick at = a.contains ("start_beat") ? std::max<Tick> (0, ticksOf (numberOr (a, "start_beat", 0))) : c.cursor;
        const bool ok = flagOr (a, "split_at_transients", false) ? c.importSplit (f, track, at) : c.importFile (f, track, at);
        return ok ? Reply::ok ("Imported " + f.getFileName().toStdString() + ".") : Reply::fail (c.message.toStdString());
    });

    //--------------------------------------------------------------------------
    // Transport, tempo, loop
    tool ("transport", "Play, stop, record, go to the start, or move the cursor to a beat (locate).",
          object ({ { "action", oneOf ("What to do.", { "play", "stop", "record", "go_to_start", "locate" }) },
                    { "beat", number ("locate: where, in beats from the song's start.") } }, { "action" }), [&c] (const json& a)
    {
        const auto action = textOr (a, "action");
        if (action == "play") c.play();
        else if (action == "stop") c.stop();
        else if (action == "record") c.record();
        else if (action == "go_to_start") c.goToStart();
        else if (action == "locate") c.locate (std::max<Tick> (0, ticksOf (numberOr (a, "beat", 0))));
        else return Reply::fail ("Unknown action " + action + ".");
        return Reply::ok (std::string ("Done. ") + (c.engine.isPlaying() ? "Playing" : "Stopped") + " at beat " + std::to_string (beatsOf (c.playhead())) + ".");
    });

    tool ("set_tempo", "Sets the song's tempo in BPM (the whole song, or from a beat on).",
          object ({ { "bpm", number ("Beats a minute, 20 to 999.") }, { "from_beat", number ("From here on; the whole song when left out.") } }, { "bpm" }),
          [&c] (const json& a)
    {
        const double bpm = jlimit (TempoMap::minBpm, TempoMap::maxBpm, numberOr (a, "bpm", 120));
        const Tick from = ticksOf (numberOr (a, "from_beat", 0));
        change (c, "Tempo " + String (bpm, 2), [&] (Project& p) { p.tempo.setTempoAt (from, bpm); });
        return Reply::ok ("The tempo is " + String (bpm, 2).toStdString() + " BPM.");
    });

    tool ("set_loop", "Sets the loop range (beats from the song's start) and turns it on or off.",
          object ({ { "start_beat", number ("Where it starts.") }, { "end_beat", number ("Where it ends.") }, { "on", flag ("Looping.") } }),
          [&c] (const json& a)
    {
        const auto& l = c.project.loop;
        const Tick s = a.contains ("start_beat") ? ticksOf (numberOr (a, "start_beat", 0)) : l.start;
        const Tick e = a.contains ("end_beat") ? ticksOf (numberOr (a, "end_beat", 0)) : l.end;
        if (e <= s) return Reply::fail ("The loop's end must be after its start.");
        const bool on = flagOr (a, "on", true);
        change (c, "Loop", [&] (Project& p) { p.loop.start = s; p.loop.end = e; p.loop.on = on; });
        return Reply::ok ("The loop is beats " + std::to_string (beatsOf (s)) + " to " + std::to_string (beatsOf (e)) + (on ? ", on." : ", off."));
    });

    tool ("undo", "Undoes the last change (an agent's or the musician's).", object ({}), [&c] (const json&)
    {
        const auto label = c.history.undoLabel();
        return c.undo() ? Reply::ok ("Undid " + label + ".") : Reply::fail ("Nothing to undo.");
    });
    tool ("redo", "Redoes what was last undone.", object ({}), [&c] (const json&)
    {
        const auto label = c.history.redoLabel();
        return c.redo() ? Reply::ok ("Redid " + label + ".") : Reply::fail ("Nothing to redo.");
    });
    tool ("song_version", "Plays song version 1 or 2 (decision 0103): the whole song twice, each with its own tracks, clips, plug-ins, "
                          "mix, tempo and key. Choosing 2 when there's only one makes it, a copy of 1. The version not playing has its "
                          "plug-ins unloaded. One undo step. get_song says which plays.",
          object ({ { "version", integer ("1 or 2.") } }, { "version" }), [&c] (const json& a)
    {
        const int v = a.contains ("version") && a["version"].is_number_integer() ? a["version"].get<int>() : 0;
        if (v != 1 && v != 2) return Reply::fail ("The version is 1 or 2.");
        c.chooseSongVersion (v - 1);
        if (c.project.songVersion != v - 1) return Reply::fail (c.message.toStdString());
        return Reply::ok ("Song version " + std::to_string (v) + " plays (" + std::to_string (c.project.songVersions()) + " in all).");
    });

    //--------------------------------------------------------------------------
    tool ("export_mix", "Exports the mix (the loop range when the loop is on, otherwise the whole song) to a file: WAV, AIFF, FLAC or MP3 "
                        "by its extension. record_tail lets what's sounding ring out past the end; second_pass exports the range's "
                        "second time through, so it loops seamlessly.",
          object ({ { "file", text ("The full path to write, e.g. /Users/me/Music/Mix.wav.") }, { "record_tail", flag ("Ring out past the end (on when left out).") },
                    { "second_pass", flag ("The range's second time through.") }, { "every_track", flag ("A file per track instead of the mix.") } }, { "file" }),
          [&c] (const json& a)
    {
        const File f (String (textOr (a, "file")));
        if (! File::isAbsolutePath (f.getFullPathName())) return Reply::fail ("Give the full path.");
        Engine::RenderOptions o;
        const auto& p = c.project;
        o.start = p.loop.on ? p.tempo.secondsAt (p.loop.start) : 0.0;
        o.end = p.loop.on ? p.tempo.secondsAt (p.loop.end) : p.tempo.secondsAt (p.songEnd());
        o.format = f.getFileExtension().trimCharactersAtStart (".").toLowerCase();
        if (o.format != "aiff" && o.format != "flac" && o.format != "mp3") o.format = "wav";
        o.tail = 0;
        o.ringOut = flagOr (a, "record_tail", true);
        o.secondPass = flagOr (a, "second_pass", false);
        f.getParentDirectory().createDirectory();
        const bool ok = c.exportAudio (f, o, flagOr (a, "every_track", false));
        return ok ? Reply::ok ("Exported to " + f.getFullPathName().toStdString() + ".") : Reply::fail (c.message.toStdString());
    });

    tool ("run_script", "Runs a Lua script against the song, for anything the other tools don't do: the `kalibre` table (tracks, clips, notes, "
                        "tempo, time signatures, scale, the selection; docs/SCRIPTING.md in Kalibre's repository). One undo step; what it "
                        "print()s comes back. e.g. for _, t in ipairs(kalibre.tracks()) do print(t.id, t.name) end",
          object ({ { "code", text ("The Lua code.") } }, { "code" }), [&c] (const json& a)
    {
        String said;
        if (! c.runScriptCode (String (textOr (a, "code")), "Agent: Script", said)) return Reply::fail ("The script failed: " + said.toStdString());
        c.say ("An AI agent ran a script." + (said.isNotEmpty() ? " " + said : String()), false, true);
        return Reply::ok (said.isEmpty() ? std::string ("It ran.") : said.toStdString());
    });
}

std::unique_ptr<mcp::Server> makeAgentServer (Controller& c)
{
    auto s = std::make_unique<mcp::Server> ("Kalibre", KALIBRE_VERSION,
        "Kalibre is a DAW, open on the musician's Mac. Call get_song first: it lists every track, device and clip with the ids the "
        "other tools take. Positions are in beats (quarter notes) from the song's start; notes' starts are from their clip's start. "
        "Every change is one undo step the musician can take back; tell them what you did.");
    addAgentTools (*s, c);
    return s;
}

} // namespace kal
