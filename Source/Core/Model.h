/*
    Model - everything a Kalibre project is, as plain data.

    One project holds tracks in the order they are shown. A track can be a
    folder (decision 0005): it holds the tracks after it whose parent it is,
    sums their sound and has devices of its own, and folders nest to any
    depth. Every ordinary track carries audio and MIDI together (decision
    0003): a clip is one or the other, and both kinds sit side by side on the
    same lane.

    A track has versions (decision 0006), four at most. Each version is a
    complete set of clips and its own devices (decision 0050); one is active.
    The active version's devices are the track's `devices`; the others wait
    in their versions. A device keeps its id in every version it's copied
    to, so the track's automation and modulators follow whichever version
    plays. New Version copies the active one, so nothing is lost. The mix
    and automation are shared by all versions.

    Two kinds of track besides: a Summing track records the master's output
    when it's armed (decision 0052), and the Cue track (one at most, kept in
    `Project::cue`) comes after the master: heard, never exported (decision
    0051).

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

// One moment of a note's own expression (MPE, decision 0042): ticks from the note's start.
struct NoteExpression
{
    Tick tick = 0;
    double bend = 0;           // semitones from the note's pitch
    double pressure = 0;       // 0-1
    double timbre = 0.5;       // 0-1 (the slide, CC 74)
    bool operator== (const NoteExpression&) const = default;
};

// Which editor made a note (decision 0059). The piano roll shows and edits
// every note; the drum grid, the step sequencer and the Euclidean sequencer
// each show and change only their own, so all of them can add to one clip.
// Played, recorded, imported and drawn notes are the piano roll's.
enum class NoteFrom { Roll, Drums, Steps, Euclid };

struct Note
{
    Id id = 0;
    Tick start = 0;
    Tick length = PPQ;
    int pitch = 60;           // 0-127
    int velocity = 100;       // 1-127
    int channel = 0;          // 0-15
    int articulation = -1;    // index into the track's articulation map; -1 none
    std::vector<NoteExpression> expression;   // MPE: bends, pressure, slide of this note alone
    NoteFrom from = NoteFrom::Roll;
    // Played differently each time (decision 0061): how often it plays at all,
    // how much softer or harder at most, how many ms early or late at most.
    int chance = 100;          // %
    int velocityRange = 0;     // either side of its velocity
    double timingMs = 0;       // either side of its start
    // A pattern step played several times in its length (decision 0063): the
    // first of them says how many; the rest are 0, "a repeat of the one before".
    int repeat = 1;

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
    // Where each edge really is, in seconds from its tick: under a tick either
    // way, so an edge can sit on the very sample of a zero crossing (decision 0054).
    double startFine = 0, endFine = 0;

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
    Id sidechainFrom = 0;      // a track whose sound feeds the plug-in's sidechain input (decision 0031)
    std::string araState;      // base64; what an ARA plug-in keeps about its edits
    float editorScale = 1.0f;  // last scale the lower zone gave its editor
    std::string stateB;        // A/B (decision 0039): the other of the two settings being compared
    bool onB = false;          // B is the one loaded
    int stateVersion = 0;      // counts Kalibre's own changes to the state (a preset, A/B), for Undo
    bool matchLoudness = false; // kept as loud coming out as going in (decision 0056)

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

// A pattern editor's feel for a clip (decision 0063): every second step
// pushed late by `swing` of a step (0-0.75), each note `gate` of its step long.
struct PatternFeel
{
    double swing = 0;
    double gate = 0.9;
    bool operator== (const PatternFeel&) const = default;
};

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
    Id fromTake = 0;          // made by the comp from this take (decision 0029); 0 for an ordinary clip
    Id link = 0;              // linked clips (decision 0037) share this: an edit to one's content is made to all
    PatternFeel drumsFeel { 0, 0.9 }, stepsFeel { 0, 0.5 }, euclidFeel { 0, 0.9 };   // decision 0063

    Tick end() const { return start + length; }
    bool isMidi() const { return kind == Kind::Midi; }
    bool operator== (const Clip&) const = default;
};

// One pass of recording (decision 0029): every pass of a loop is a take.
struct Take
{
    Id id = 0;
    std::string name;          // "Take 3"
    std::vector<Clip> clips;

    bool operator== (const Take&) const = default;
};

// Which take plays over a stretch of time: the comp.
struct CompSegment
{
    Tick start = 0, end = 0;
    Id take = 0;

    bool operator== (const CompSegment&) const = default;
};

inline constexpr int maxVersions = 4;

struct TrackVersion
{
    Id id = 0;
    std::string name;          // "Version 2"
    std::vector<Clip> clips;   // sorted by start
    std::vector<Take> takes;   // oldest first
    std::vector<CompSegment> comp;
    bool takesShown = false;   // the take lanes open under the track
    // Its devices while another version is active (decision 0050); empty for
    // the active one, whose devices are the track's. A version made before
    // versions had devices of their own takes the track's when chosen.
    std::vector<Device> devices;
    bool ownDevices = false;

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

enum class TrackKind { Track, Folder, Return, Master, Summing, Cue };
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
    double frontBack = 1.0;      // in a surround mix: -1 behind .. 1 in front (decision 0040)
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
    // The CC lanes shown under its notes, in the piano roll and notation (decision 0060).
    std::vector<int> ccLanes { 1, 11 };

    // The clip launcher (decision 0037): one slot a scene; a slot whose length is 0 is empty.
    std::vector<Clip> slots;
    struct Launch
    {
        int slot = -1;         // the slot playing, or -1: the arrangement plays
        Tick from = 0;         // where it took over (or stopped), on the next bar after it was asked
        bool stopped = false;  // silent from `from` until Back to Arrangement
        bool operator== (const Launch&) const = default;
    };
    Launch launch;             // not saved: what's playing now

    bool mpe = false;          // plays each note on its own channel with its own expression (decision 0042)
    Id sourceDevice = 0;       // an instrument output track (decision 0040): plays this instrument's
    int sourcePair = 0;        //   outputs 2*pair and 2*pair+1 instead of clips

    TrackVersion& version()             { return versions[static_cast<size_t> (std::clamp (activeVersion, 0, static_cast<int> (versions.size()) - 1))]; }
    const TrackVersion& version() const { return versions[static_cast<size_t> (std::clamp (activeVersion, 0, static_cast<int> (versions.size()) - 1))]; }
    std::vector<Clip>& clips()             { return version().clips; }
    const std::vector<Clip>& clips() const { return version().clips; }

    bool isFolder() const { return kind == TrackKind::Folder; }
    bool holdsClips() const { return kind == TrackKind::Track || kind == TrackKind::Summing; }
    bool isCue() const { return kind == TrackKind::Cue; }

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

// A knob, fader or button on a MIDI controller, learned onto a control (decision 0032).
struct MidiMapping
{
    int channel = 0;           // 1-16, 0 any
    int controller = 1;        // CC number; 128 for pitch bend
    Id track = 0;              // whose control
    AutoTarget target;         // Volume, Pan, Mute or DeviceParam
    std::string name;          // shown in the list
    bool operator== (const MidiMapping&) const = default;
};

// A named stretch of the song (decision 0037): Verse, Chorus. Moving,
// duplicating or deleting a section does it to everything in it.
struct Section
{
    Id id = 0;
    std::string name;
    Tick start = 0, end = PPQ * 16;
    std::uint32_t colour = 0;
    bool operator== (const Section&) const = default;
};

// What an export carries in its file (decision 0041).
struct Metadata
{
    std::string title, artist, album, genre, comment, isrc;
    int year = 0, trackNumber = 0;
    bool operator== (const Metadata&) const = default;
};

struct Video
{
    std::string file;          // empty: none
    Tick start = 0;            // where its first frame is on the timeline
    bool operator== (const Video&) const = default;
};

enum class SurroundFormat { Stereo, Quad, Surround51, Surround71 };

struct Project
{
    std::string name = "Untitled";
    TempoMap tempo;
    std::vector<Track> tracks;          // display order; folders before their children
    Track master;
    std::vector<Track> cue;             // the Cue track, if there is one: after the master, never exported (decision 0051)
    std::vector<Marker> markers;
    Range loop, punch;
    ScaleChoice scale;
    MetronomeSettings metronome;
    std::vector<ArticulationMap> articulationMaps;
    std::vector<MidiMapping> midiMappings;
    std::vector<Section> sections;      // sorted by start
    std::vector<std::string> scenes;    // the launcher's rows, named
    Metadata metadata;
    Video video;
    SurroundFormat surround = SurroundFormat::Stereo;   // decision 0040
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
