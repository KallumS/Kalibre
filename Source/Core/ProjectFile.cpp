#include "ProjectFile.h"

#include <nlohmann/json.hpp>

namespace kal
{
using nlohmann::json;

NLOHMANN_JSON_SERIALIZE_ENUM (EventType, { { EventType::Controller, "cc" }, { EventType::PitchBend, "bend" },
                                           { EventType::ChannelPressure, "pressure" }, { EventType::PolyPressure, "polyPressure" },
                                           { EventType::ProgramChange, "program" } })
NLOHMANN_JSON_SERIALIZE_ENUM (WarpMode, { { WarpMode::Off, "off" }, { WarpMode::Repitch, "repitch" },
                                          { WarpMode::Granular, "granular" }, { WarpMode::Spectral, "spectral" } })
NLOHMANN_JSON_SERIALIZE_ENUM (DeviceRole, { { DeviceRole::MidiFx, "midiFx" }, { DeviceRole::Instrument, "instrument" },
                                            { DeviceRole::AudioFx, "audioFx" } })
NLOHMANN_JSON_SERIALIZE_ENUM (Clip::Kind, { { Clip::Kind::Midi, "midi" }, { Clip::Kind::Audio, "audio" } })
NLOHMANN_JSON_SERIALIZE_ENUM (AutoTarget::Kind, { { AutoTarget::Kind::Volume, "volume" }, { AutoTarget::Kind::Pan, "pan" },
                                                  { AutoTarget::Kind::Mute, "mute" }, { AutoTarget::Kind::SendLevel, "send" },
                                                  { AutoTarget::Kind::DeviceParam, "param" }, { AutoTarget::Kind::MorphX, "morphX" },
                                                  { AutoTarget::Kind::MorphY, "morphY" } })
NLOHMANN_JSON_SERIALIZE_ENUM (AutoMode, { { AutoMode::Off, "off" }, { AutoMode::Read, "read" }, { AutoMode::Touch, "touch" },
                                          { AutoMode::Latch, "latch" }, { AutoMode::Write, "write" }, { AutoMode::Trim, "trim" } })
NLOHMANN_JSON_SERIALIZE_ENUM (ModKind, { { ModKind::Lfo, "lfo" }, { ModKind::Adsr, "adsr" }, { ModKind::Adr, "adr" },
                                         { ModKind::EnvelopeFollower, "follower" }, { ModKind::Wavetable, "wavetable" } })
NLOHMANN_JSON_SERIALIZE_ENUM (LfoShape, { { LfoShape::Sine, "sine" }, { LfoShape::Triangle, "triangle" },
                                          { LfoShape::SawUp, "sawUp" }, { LfoShape::SawDown, "sawDown" },
                                          { LfoShape::Square, "square" }, { LfoShape::SampleAndHold, "sampleAndHold" } })
NLOHMANN_JSON_SERIALIZE_ENUM (TrackKind, { { TrackKind::Track, "track" }, { TrackKind::Folder, "folder" },
                                           { TrackKind::Return, "return" }, { TrackKind::Master, "master" },
                                           { TrackKind::Summing, "summing" }, { TrackKind::Cue, "cue" } })
NLOHMANN_JSON_SERIALIZE_ENUM (Monitor, { { Monitor::Auto, "auto" }, { Monitor::In, "in" }, { Monitor::Off, "off" } })

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (TempoPoint, tick, bpm, rampToNext)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (Meter, bar, numerator, denominator)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (NoteExpression, tick, bend, pressure, timbre)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (Note, id, start, length, pitch, velocity, channel, articulation, expression, from,
                                                 chance, velocityRange, timingMs, repeat)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (MidiEvent, tick, type, channel, number, value)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (MidiData, notes, events)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (WarpMarker, sourceSeconds, tick)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (AudioData, file, sourceStart, sourceLength, fileSampleRate, fileChannels, gainDb,
                                                 fadeIn, fadeOut, reversed, transpose, warp, sourceBpm, markers, renderedFile,
                                                 startFine, endFine)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (PluginRef, format, identifier, name, manufacturer, uid, isInstrument, description)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (Device, id, role, plugin, state, bypassed, active, useAra, araState, sidechainFrom, editorScale,
                                                 stateB, onB, matchLoudness)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (OfflineFx, chain, includeTail)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (PatternFeel, swing, gate)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (Clip, id, kind, name, start, length, offset, loopLength, colour, muted, locked,
                                                 midi, audio, offlineFx, fromTake, link, drumsFeel, stepsFeel, euclidFeel)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (Take, id, name, clips)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (CompSegment, start, end, take)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (TrackVersion, id, name, clips, takes, comp, takesShown, devices, ownDevices)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (AutoTarget, kind, device, param, send, paramName)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (AutoPoint, tick, value, curve, step)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (AutomationLane, id, target, points, shown)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (ModTarget, device, param, amount, paramName)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (Modulator, id, kind, name, shape, rateHz, sync, syncBeats, phase, bipolar,
                                                 attack, decay, sustain, release, sourceTrack, followAttack, followRelease,
                                                 followGain, table, tableFile, targets)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (Articulation, name, keyswitch, keyswitchVelocity, cc, ccValue, program, channel,
                                                 delayMs, colour)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (ArticulationMap, id, name, articulations)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (Send, target, gainDb, preFader, muted)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (TrackInput, audioChannel, stereo, midiDevice, midiChannel)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (Track, id, name, colour, kind, parent, collapsed, height, volumeDb, pan, mute, solo,
                                                 armed, monitor, phaseInvert, mono, locked, hidden, enabled, frozen, frozenFile,
                                                 delayMs, group, input, output, versions, activeVersion, devices, sends,
                                                 automation, autoMode, modulators, articulationMap,
                                                 slots, mpe, sourceDevice, sourcePair, frontBack, ccLanes, armFor)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (Marker, id, tick, name)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (Section, id, name, start, end, colour)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (Metadata, title, artist, album, genre, comment, isrc, year, trackNumber)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (Video, file, start)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (MidiMapping, channel, controller, track, target, name)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (Range, start, end, on)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (ScaleChoice, root, scale, highlight)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT (MetronomeSettings, on, countIn, countInBars, preRollBars, volumeDb, subdivision,
                                                 accentFirst, onlyWhileRecording)

void to_json (json& j, const TempoMap& t)
{
    j = json { { "tempos", t.tempos() }, { "meters", t.meters() } };
}

void from_json (const json& j, TempoMap& t)
{
    t = TempoMap();
    if (j.contains ("tempos")) t.setTempos (j.at ("tempos").get<std::vector<TempoPoint>>());
    if (j.contains ("meters")) t.setMeters (j.at ("meters").get<std::vector<Meter>>());
}

} // namespace kal

namespace kal::file
{

std::string save (const Project& p)
{
    nlohmann::json j;
    j["kalibre"] = formatVersion;
    j["name"] = p.name;
    j["tempo"] = p.tempo;
    j["tracks"] = p.tracks;
    j["master"] = p.master;
    j["cue"] = p.cue;
    j["markers"] = p.markers;
    j["loop"] = p.loop;
    j["punch"] = p.punch;
    j["scale"] = p.scale;
    j["metronome"] = p.metronome;
    j["articulationMaps"] = p.articulationMaps;
    j["midiMappings"] = p.midiMappings;
    j["sections"] = p.sections;
    j["scenes"] = p.scenes;
    j["metadata"] = p.metadata;
    j["video"] = p.video;
    j["surround"] = p.surround;
    j["sampleRate"] = p.sampleRate;
    j["nextId"] = p.nextId;
    return j.dump (1, '\t');
}

std::optional<Project> load (const std::string& text, std::string* error)
{
    try
    {
        const auto j = nlohmann::json::parse (text);
        if (! j.is_object() || ! j.contains ("kalibre"))
        {
            if (error) *error = "This isn't a Kalibre project.";
            return std::nullopt;
        }
        if (j.at ("kalibre").get<int>() > formatVersion)
        {
            if (error) *error = "This project was saved by a newer Kalibre.";
            return std::nullopt;
        }
        Project p;
        p.name = j.value ("name", p.name);
        if (j.contains ("tempo")) p.tempo = j.at ("tempo").get<TempoMap>();
        if (j.contains ("tracks")) p.tracks = j.at ("tracks").get<std::vector<Track>>();
        if (j.contains ("master")) p.master = j.at ("master").get<Track>();
        if (j.contains ("cue")) p.cue = j.at ("cue").get<std::vector<Track>>();
        if (p.cue.size() > 1) p.cue.resize (1);
        for (auto& c : p.cue) { c.kind = TrackKind::Cue; c.versions.resize (1); c.activeVersion = 0; }
        p.master.kind = TrackKind::Master;
        if (j.contains ("markers")) p.markers = j.at ("markers").get<std::vector<Marker>>();
        if (j.contains ("loop")) p.loop = j.at ("loop").get<Range>();
        if (j.contains ("punch")) p.punch = j.at ("punch").get<Range>();
        if (j.contains ("scale")) p.scale = j.at ("scale").get<ScaleChoice>();
        if (j.contains ("metronome")) p.metronome = j.at ("metronome").get<MetronomeSettings>();
        if (j.contains ("articulationMaps")) p.articulationMaps = j.at ("articulationMaps").get<std::vector<ArticulationMap>>();
        if (j.contains ("midiMappings")) p.midiMappings = j.at ("midiMappings").get<std::vector<MidiMapping>>();
        if (j.contains ("sections")) p.sections = j.at ("sections").get<std::vector<Section>>();
        if (j.contains ("scenes")) p.scenes = j.at ("scenes").get<std::vector<std::string>>();
        if (j.contains ("metadata")) p.metadata = j.at ("metadata").get<Metadata>();
        if (j.contains ("video")) p.video = j.at ("video").get<Video>();
        if (j.contains ("surround")) p.surround = j.at ("surround").get<SurroundFormat>();
        p.sampleRate = j.value ("sampleRate", p.sampleRate);
        p.nextId = j.value ("nextId", p.nextId);

        // Never hand out an id already in use, whatever the file said.
        Id highest = p.master.id;
        auto see = [&highest] (Id id) { highest = std::max (highest, id); };
        for (auto& t : p.tracks)
        {
            see (t.id);
            if (t.versions.empty()) t.versions.push_back ({});
            for (auto& v : t.versions)
            {
                see (v.id);
                for (auto& d : v.devices) see (d.id);
                for (auto& c : v.clips) { see (c.id); see (c.link); for (auto& n : c.midi.notes) see (n.id); }
                for (auto& tk : v.takes) { see (tk.id); for (auto& c : tk.clips) { see (c.id); for (auto& n : c.midi.notes) see (n.id); } }
            }
            for (auto& c : t.slots) { see (c.id); for (auto& n : c.midi.notes) see (n.id); }
            for (auto& d : t.devices) see (d.id);
            for (auto& a : t.automation) see (a.id);
            for (auto& m : t.modulators) see (m.id);
        }
        for (auto& d : p.master.devices) see (d.id);
        for (auto& c : p.cue) { see (c.id); for (auto& d : c.devices) see (d.id); }
        for (auto& m : p.markers) see (m.id);
        for (auto& sct : p.sections) see (sct.id);
        for (auto& m : p.articulationMaps) see (m.id);
        p.nextId = std::max (p.nextId, highest + 1);
        return p;
    }
    catch (const std::exception& e)
    {
        if (error) *error = std::string ("The project couldn't be read: ") + e.what();
        return std::nullopt;
    }
}

} // namespace kal::file
