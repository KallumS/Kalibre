/*
    Model - everything a Kalibre project is, as plain data.

    One project holds tracks in the order they are shown. A track can be a
    folder (decision 0005): it holds the tracks after it whose parent it is,
    sums their sound and has devices of its own, and folders nest to any
    depth. Every ordinary track carries audio and MIDI together (decision
    0003): a clip is one or the other, and both kinds sit side by side on the
    same lane.

    A track has versions (decision 0006). Each version is a complete set of
    clips; one is active. New Version copies the active one, so nothing is
    lost, and a version's clips stay until that version is deleted. The
    track's devices, mix and automation are shared by all its versions.

    Devices are plug-ins (AU, VST3, CLAP) or Kalibre's own (the X/Y morph).
    They run MIDI effects, then instruments, then audio effects, in that
    order whatever order they were added in (decision 0007). A device can be
    deactivated: it is unloaded from memory and the processor, and its saved
    state is kept so it comes back exactly as it was (decision 0010).

    Nothing about the view is stored here except what a musician would call
    part of the song (a track's colour, its height, a folder being folded).
    What is selected lives in the window.

    No JUCE in here.
*/

#pragma once

#include "TempoMap.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kal
{

using Id = std::uint32_t;

//==============================================================================
// MIDI

struct Note
{
    Id id = 0;
    Tick start = 0;
    Tick length = PPQ;
    int pitch = 60;           // 0-127
    int velocity = 100;       // 1-127
    int channel = 0;          // 0-15
    int articulation = -1;    // index into the track's articulation map; -1 none

    Tick end() const { return start + length; }
    bool operator== (const Note&) const = default;
};

enum class EventType { Controller, PitchBend, ChannelPressure, PolyPressure, ProgramChange };

struct MidiEvent
{
    Tick tick = 0;
    EventType type = EventType::Controller;
    int channel = 0;
    int number = 1;     // CC number, or the key for poly pressure
    int value = 0;      // 0-127; pitch bend 0-16383 (8192 is centre)

    bool operator== (const MidiEvent&) const = default;
};

struct MidiData
{
    std::vector<Note> notes;          // kept sorted by start, then pitch
    std::vector<MidiEvent> events;    // kept sorted by tick

    bool operator== (const MidiData&) const = default;
};

//==============================================================================
// Audio

enum class WarpMode
{
    Off,        // plays at the file's own speed, whatever the tempo
    Repitch,    // speed and pitch change together, like tape
    Granular,   // grains: good for drums and textures
    Spectral,   // phase vocoder: good for anything tonal and for mixes
};

struct WarpMarker
{
    double sourceSeconds = 0;   // a moment in the file
    Tick tick = 0;              // where it lands, from the clip's start

    bool operator== (const WarpMarker&) const = default;
};

struct AudioData
{
    std::string file;               // absolute, or relative to the project folder
    double sourceStart = 0;         // seconds into the file the clip starts at
    double sourceLength = 0;        // seconds of file the clip holds, unwarped
    double fileSampleRate = 48000;
    int fileChannels = 2;
    double gainDb = 0;
    double fadeIn = 0, fadeOut = 0; // seconds
    bool reversed = false;
    double transpose = 0;           // semitones
    WarpMode warp = WarpMode::Off;
    double sourceBpm = 0;           // 0 when unknown
    std::vector<WarpMarker> markers;
    std::string renderedFile;       // the result of offline effects, or empty

    bool operator== (const AudioData&) const = default;
};

//==============================================================================
// Plug-ins

struct PluginRef
{
    std::string format;        // "AudioUnit", "VST3", "CLAP" or "Kalibre"
    std::string identifier;    // the file, or the AU's id string
    std::string name;
    std::string manufacturer;
    int uid = 0;
    bool isInstrument = false;
    std::string description;   // the host's own full description, for reloading exactly

    bool operator== (const PluginRef&) const = default;
};

enum class DeviceRole { MidiFx, Instrument, AudioFx };

struct Device
{
    Id id = 0;
    DeviceRole role = DeviceRole::AudioFx;
    PluginRef plugin;
    std::string state;         // base64; the plug-in's own saved state
    bool bypassed = false;
    bool active = true;        // false: unloaded, state kept (decision 0010)
    bool useAra = true;        // ARA plug-ins (Melodyne) read the clips directly
    std::string araState;      // base64; what an ARA plug-in keeps about its edits
    float editorScale = 1.0f;  // last scale the lower zone gave its editor

    bool operator== (const Device&) const = default;
};

// Offline effects on one audio clip: rendered into a new file, kept
// non-destructively so they can be changed and rendered again.
struct OfflineFx
{
    std::vector<Device> chain;
    bool includeTail = true;

    bool operator== (const OfflineFx&) const = default;
};

//==============================================================================
// Clips, versions

struct Clip
{
    enum class Kind { Midi, Audio };

    Id id = 0;
    Kind kind = Kind::Midi;
    std::string name;
    Tick start = 0;
    Tick length = PPQ * 4;
    Tick offset = 0;          // how far into its content the clip starts
    Tick loopLength = 0;      // 0: the content plays once; otherwise it repeats every loopLength
    std::uint32_t colour = 0; // 0: the track's
    bool muted = false;
    bool locked = false;
    MidiData midi;
    AudioData audio;
    OfflineFx offlineFx;

    Tick end() const { return start + length; }
    bool isMidi() const { return kind == Kind::Midi; }
    bool operator== (const Clip&) const = default;
};

struct TrackVersion
{
    Id id = 0;
    std::string name;          // "Version 2"
    std::vector<Clip> clips;   // sorted by start

    bool operator== (const TrackVersion&) const = default;
};

//==============================================================================
// Automation

struct AutoTarget
{
    enum class Kind { Volume, Pan, Mute, SendLevel, DeviceParam, MorphX, MorphY };

    Kind kind = Kind::Volume;
    Id device = 0;             // DeviceParam, MorphX/Y
    int param = -1;            // DeviceParam: the plug-in's parameter index
    Id send = 0;               // SendLevel: the target track
    std::string paramName;     // for display, and to find the parameter again if the order changed

    bool operator== (const AutoTarget& o) const
    {
        return kind == o.kind && device == o.device && param == o.param && send == o.send;
    }
};

struct AutoPoint
{
    Tick tick = 0;
    double value = 0;          // normalised, 0-1
    double curve = 0;          // -1..1 bends the segment to the next point; 0 straight
    bool step = false;         // hold until the next point

    bool operator== (const AutoPoint&) const = default;
};

enum class AutoMode { Off, Read, Touch, Latch, Write, Trim };

struct AutomationLane
{
    Id id = 0;
    AutoTarget target;
    std::vector<AutoPoint> points;   // sorted by tick
    bool shown = true;

    bool operator== (const AutomationLane&) const = default;
};

//==============================================================================
// Modulators (decision 0012)

enum class ModKind { Lfo, Adsr, Adr, EnvelopeFollower, Wavetable };
enum class LfoShape { Sine, Triangle, SawUp, SawDown, Square, SampleAndHold };

struct ModTarget
{
    Id device = 0;
    int param = -1;
    double amount = 0.5;       // -1..1 of the parameter's range
    std::string paramName;

    bool operator== (const ModTarget&) const = default;
};

struct Modulator
{
    Id id = 0;
    ModKind kind = ModKind::Lfo;
    std::string name;

    // LFO and wavetable
    LfoShape shape = LfoShape::Sine;
    double rateHz = 1.0;
    bool sync = true;
    double syncBeats = 1.0;     // one cycle every this many quarter notes
    double phase = 0.0;         // 0-1
    bool bipolar = true;

    // Envelopes (seconds; sustain 0-1). Triggered by the track's notes.
    double attack = 0.01, decay = 0.2, sustain = 0.7, release = 0.3;

    // Envelope follower
    Id sourceTrack = 0;         // 0: this track's own sound
    double followAttack = 0.01, followRelease = 0.15, followGain = 1.0;

    // Wavetable: one cycle of any shape, drawn or read from a sample
    std::vector<float> table;   // -1..1
    std::string tableFile;

    std::vector<ModTarget> targets;

    bool operator== (const Modulator&) const = default;
};

//==============================================================================
// Articulations (decision 0013)

struct Articulation
{
    std::string name;            // "Staccato"
    int keyswitch = -1;          // a note sent just before, or -1
    int keyswitchVelocity = 100;
    int cc = -1, ccValue = 0;    // a controller sent just before, or -1
    int program = -1;            // a program change, or -1
    int channel = -1;            // send the note on this channel instead, or -1
    double delayMs = 0;          // play this articulation earlier (negative) or later
    std::uint32_t colour = 0;

    bool operator== (const Articulation&) const = default;
};

struct ArticulationMap
{
    Id id = 0;
    std::string name;
    std::vector<Articulation> articulations;

    bool operator== (const ArticulationMap&) const = default;
};

//==============================================================================
// Tracks

enum class TrackKind { Track, Folder, Return, Master };
enum class Monitor { Auto, In, Off };

struct Send
{
    Id target = 0;              // a return (or any track that isn't this one's descendant)
    double gainDb = -6.0;
    bool preFader = false;
    bool muted = false;

    bool operator== (const Send&) const = default;
};

struct TrackInput
{
    int audioChannel = 0;        // first input channel; -1 none
    bool stereo = true;
    std::string midiDevice = "All";   // "All", "None" or a device's id
    int midiChannel = 0;         // 0 omni, 1-16

    bool operator== (const TrackInput&) const = default;
};

struct Track
{
    Id id = 0;
    std::string name;
    std::uint32_t colour = 0xffA9AFBA;
    TrackKind kind = TrackKind::Track;
    Id parent = 0;               // the folder it is in, 0 for none
    bool collapsed = false;      // a folder folded shut
    int height = 64;             // points

    double volumeDb = 0.0;
    double pan = 0.0;            // -1..1
    bool mute = false, solo = false, armed = false;
    Monitor monitor = Monitor::Auto;
    bool phaseInvert = false, mono = false;
    bool locked = false, hidden = false, enabled = true;
    bool frozen = false;
    std::string frozenFile;
    double delayMs = 0.0;        // negative plays earlier (decision 0011)
    int group = 0;               // linked with every track in the same group; 0 none

    TrackInput input;
    Id output = 0;               // 0: its folder, or the master

    std::vector<TrackVersion> versions { TrackVersion {} };
    int activeVersion = 0;

    std::vector<Device> devices;
    std::vector<Send> sends;
    std::vector<AutomationLane> automation;
    AutoMode autoMode = AutoMode::Read;
    std::vector<Modulator> modulators;
    Id articulationMap = 0;

    TrackVersion& version()             { return versions[static_cast<size_t> (std::clamp (activeVersion, 0, static_cast<int> (versions.size()) - 1))]; }
    const TrackVersion& version() const { return versions[static_cast<size_t> (std::clamp (activeVersion, 0, static_cast<int> (versions.size()) - 1))]; }
    std::vector<Clip>& clips()             { return version().clips; }
    const std::vector<Clip>& clips() const { return version().clips; }

    bool isFolder() const { return kind == TrackKind::Folder; }
    bool holdsClips() const { return kind == TrackKind::Track; }

    bool operator== (const Track&) const = default;
};

//==============================================================================
// The project

struct Marker
{
    Id id = 0;
    Tick tick = 0;
    std::string name;
    bool operator== (const Marker&) const = default;
};

struct Range
{
    Tick start = 0, end = PPQ * 16;
    bool on = false;
    bool operator== (const Range&) const = default;
};

struct ScaleChoice
{
    int root = 0;              // index into scaleview::roots
    int scale = 0;             // index into scaleview::scales
    bool highlight = true;     // shade the scale's rows in the piano roll
    bool operator== (const ScaleChoice&) const = default;
};

struct MetronomeSettings
{
    bool on = false;
    bool countIn = false;
    int countInBars = 1;
    int preRollBars = 0;
    double volumeDb = -6.0;
    int subdivision = 1;       // clicks per beat
    bool accentFirst = true;
    bool onlyWhileRecording = false;
    bool operator== (const MetronomeSettings&) const = default;
};

struct Project
{
    std::string name = "Untitled";
    TempoMap tempo;
    std::vector<Track> tracks;          // display order; folders before their children
    Track master;
    std::vector<Marker> markers;
    Range loop, punch;
    ScaleChoice scale;
    MetronomeSettings metronome;
    std::vector<ArticulationMap> articulationMaps;
    double sampleRate = 48000;
    Id nextId = 1;

    Project();

    Id newId() { return nextId++; }

    Track* track (Id);
    const Track* track (Id) const;
    int indexOf (Id) const;              // -1 when not found or the master

    Clip* clip (Id clipId, Track** owner = nullptr);
    const Clip* clip (Id clipId, const Track** owner = nullptr) const;

    Device* device (Id deviceId, Track** owner = nullptr);
    const ArticulationMap* articulationMap (Id) const;

    // Folders ----------------------------------------------------------------
    int depth (Id) const;                         // 0 at the top level
    bool isInside (Id track, Id folder) const;    // anywhere below it
    std::vector<Id> childrenOf (Id folder) const; // direct children, in order
    bool hiddenByFolder (Id) const;               // inside a folded folder
    Id outputOf (Id) const;                       // where its sound goes: a track id, or 0 for the master

    // The end of the last clip, rounded up to a bar; at least 16 bars.
    Tick endTick() const;

    bool operator== (const Project&) const = default;
};

} // namespace kal
