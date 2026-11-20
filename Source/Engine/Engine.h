/*
    Engine - the audio graph, the transport, plug-in instances, MIDI in.

    The project is turned into a juce::AudioProcessorGraph (decision 0016):
    a source, devices and a fader for every track, folders summing what's
    inside them, sends feeding returns, everything into the master. The graph
    lines up every path that meets at a sum by delaying the quicker ones -
    that is plug-in delay compensation, for AU, VST3 and CLAP alike.

    `sync` is called whenever the project changes. It keeps every plug-in
    instance that is still wanted (so a plug-in never reloads because a note
    moved), makes the ones that are new, drops deactivated ones (decision
    0010), rewires only when the shape of the project changed, and hands
    each track's playback lists to its source.

    The transport lives here: the audio callback moves the position, wraps
    the loop inside a block, and tells every node where the block is.
*/

#pragma once

#include "AraHost.h"
#include "Loudness.h"
#include "MidiLearn.h"
#include "ParallelGraph.h"
#include "PluginManager.h"
#include "SyncDevices.h"
#include "TrackProcessors.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <map>
#include <mutex>
#include <set>

namespace kal
{

class Engine : public juce::AudioIODeviceCallback,
               private juce::MidiInputCallback,
               private juce::AudioProcessorListener,
               private juce::Timer,
               private juce::AudioPlayHead
{
public:
    explicit Engine (PluginManager&);
    ~Engine() override;

    // Opens the audio device (the last one used, or the default). Returns an error, if any.
    juce::String startAudio (bool withInputs = true);
    juce::AudioDeviceManager& devices() { return deviceManager; }
    PluginManager& plugins() { return pluginManager; }

    //==========================================================================
    // The project (message thread).
    void sync (const Project&, Id liveTrack, const juce::File& projectFolder);

    juce::AudioPluginInstance* instance (Id device) const;
    // Load (decision 0038): each track's plug-ins' share of real time, and blocks that came late.
    double trackCpu (Id track) const;
    // What Match Loudness is doing to a device's output now, in dB (decision 0056).
    float matchingDb (Id device) const;
    double cpu() { return deviceManager.getCpuUsage(); }
    // Every performance core (decision 0078), or the audio thread alone.
    void setParallel (bool on) { graph.setParallel (on); }
    bool isParallel() const { return graph.isParallel(); }
    int audioThreads() const { return graph.workers() + 1; }
    ParallelGraph& audioGraph() { return graph; }   // tests
    int dropouts() const;
    void resetDropouts();
    // A different song (opened, new, from a template, by chance): every plug-in the
    // last one had is let go of. Devices are numbered from 1 in every song, so an
    // instance kept by its number would play the next song with the last one's settings.
    void newSong();
    int loadedPlugins() const { return (int) allDevices.size(); }   // plug-in instances in memory (tests, 0103)
    Fader* fader (Id track) const;
    TrackSource* source (Id track) const;
    // Where a track's recording is written: its source, or for a Summing
    // track the recorder hearing the master (decision 0052).
    TrackSource* recordSource (Id track) const;
    int latencySamples();
    double sampleRate() const { return rate; }

    // A device's saved state, as the project keeps it (base64); `version`
    // for a device of a version that isn't playing (decision 0050).
    std::string stateOf (Id device) const;
    std::string stateOf (Id device, Id version) const;
    std::string araStateOf (Id device) const;
    // How many channels a plug-in's sidechain input has (0: none).
    int sidechainChannels (Id device) const;
    static bool enableSidechain (juce::AudioProcessor&);
    bool playedByAra (Id track) const;

    //==========================================================================
    // Transport.
    void play();
    void stop();
    void setRecording (bool);
    bool isPlaying() const { return playing.load(); }
    bool isRecording() const { return recording.load(); }
    void locate (double seconds);
    double position() const { return pos.load(); }
    // Where the music heard right now is: the position less what the graph delays it by.
    double heardPosition();
    void setLoop (bool on, double start, double end);
    void setCountIn (double seconds) { countInSeconds = seconds; }

    //==========================================================================
    // Playing a note to hear it (clicking in the piano roll).
    // A message as if played on a keyboard (the tests play notes in with it).
    void playIn (const juce::MidiMessage& m) { handleIncomingMidiMessage (nullptr, m); }
    std::atomic<juce::uint32> notesPlayedIn { 0 };   // notes from MIDI keyboards, for the top bar's timer (0091)
    void audition (Id track, int pitch, int velocity = 100, double seconds = 0.3);

    //==========================================================================
    // Last touched (decision 0024): whatever control a hand moves last.
    struct Touch
    {
        Id device = 0;
        int param = -1;
        juce::String name;
        float value = 0;
        int gesture = 0;   // 1 began, 0 a change, 2 ended
    };
    std::function<void (const Touch&)> onTouched;

    // MIDI learn (decision 0032).
    std::atomic<bool> learning { false };
    // Step input (decision 0076): keys played in are told to the window too, as they're played.
    std::atomic<bool> stepInputOn { false };
    std::function<void (int pitch, int velocity, bool on)> onStepKey;
    void setMappings (std::vector<MidiMapping>);
    std::function<void (int channel, int controller)> onLearned;
    std::function<void (const MidiMapping&, double value)> onMapped;

    // Messages for control surfaces and sync, taken out before the tracks hear them.
    std::function<bool (const juce::String& device, const juce::MidiMessage&)> surfaceInput;
    std::function<void (const juce::String& device, const juce::MidiMessage&)> syncInput;
    std::function<void (const juce::String&)> onMessage;
    std::function<void()> onAudioReady;
    std::function<void()> onDevicePluginChanged;   // a plug-in's own state changed

    //==========================================================================
    struct RenderOptions
    {
        double start = 0, end = 0;          // seconds
        double tail = 2.0;                  // seconds more of the song, played on past the end
        // Record the Tail (0097): after the end nothing new plays, and what's sounding
        // (releases, reverbs, delays) rings until it's silent, at most ringOutMax seconds.
        bool ringOut = false;
        double ringOutMax = 30.0;
        // The Second Pass (0097): the range played once unheard, then exported the second
        // time through, so what rings past its end is heard at its start (a loop that loops).
        bool secondPass = false;
        double sampleRate = 48000;
        int bitDepth = 24;
        bool normalise = false;
        double loudnessTarget = 0;          // LUFS (e.g. -14); 0 leaves the level alone
        double truePeakCeiling = -1.0;      // dBTP the loudness gain may not push past
        bool dither = true;
        juce::String format = "wav";        // wav, aiff, flac, mp3
        int mp3Kbps = 320;
        Metadata metadata;                  // the song's details, written into the file (decision 0041)
    };
    // Renders the project as it would play (not in real time) into a file.
    // The device is paused while it works. `progress` returning false cancels.
    bool render (const Project&, const juce::File& folder, const juce::File& out, const RenderOptions&,
                 std::function<bool (float)> progress, juce::String& error);

    // One track, through its own plug-ins, into a 32-bit float WAV (freeze and bounce,
    // decision 0080). The others' plug-ins stay loaded but aren't played.
    bool renderTrack (const Project&, Id track, const juce::File& folder, const juce::File& out,
                      double start, double end, double tail, juce::String& error);

    // Offline effects on one audio clip: its audio through a chain, into a new file.
    bool renderOfflineFx (const Clip&, const juce::File& source, const juce::File& out, juce::String& error);

    // Consolidate (decision 0081): some of a track's audio clips, as they play (gain,
    // fades, warping, loops; no plug-ins), mixed at 64 bits from `from` to `to` into a
    // new 32-bit float WAV at the song's rate, normalised to 0 dBFS. `peak` is the
    // level it was turned up from: the new clip's gain, so it plays as loud as before.
    bool renderClips (const Project&, Id track, const std::set<Id>& clips, Tick from, Tick to,
                      const juce::File& folder, const juce::File& out, double& peak, juce::String& error);

    Shared shared;
    AudioCache cache;
    SyncDevices syncDevices;   // MIDI clock, timecode, Link (decision 0034)
    double lastExportLoudness = loudness::silence;   // what the last export measured, after its gain

    // AudioIODeviceCallback (the audio thread)
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

private:
    PluginManager& pluginManager;
    std::mutex mappingLock;
    std::vector<MidiMapping> mappings;
    juce::AudioDeviceManager deviceManager;
    ParallelGraph graph;   // played on every performance core (decision 0078)
    juce::AudioProcessorPlayer player;

    using NodeID = juce::AudioProcessorGraph::NodeID;
    using Node = juce::AudioProcessorGraph::Node;

    struct TrackNodes
    {
        Node::Ptr source, fader;
        Node::Ptr recorder;   // a Summing track's: hears the master, records it (decision 0052)
        std::map<Id, Node::Ptr> sends;
    };
    std::map<Id, TrackNodes> tracks;
    // Every version's devices, by (version, device): a device keeps its id in
    // each version it's copied to, with an instance of its own (decision 0050).
    using VersionedId = std::pair<Id, Id>;
    std::map<VersionedId, Node::Ptr> allDevices;
    std::map<VersionedId, std::string> allKeys;   // what each node was made from, to notice a swap
    std::map<Id, Node::Ptr> deviceNodes;          // the playing versions' devices, by device id
    std::map<Id, Id> trackOf;                     // each playing device's track
    void lineUpAutomation();                      // each fader, send and plug-in told how late it hears (0096)
    std::map<std::pair<Id, int>, Node::Ptr> splitNodes;
    std::map<Id, Node::Ptr> heldDelays;   // switched-off effects' delays, by device (decision 0086)
    int heldSamples (const Device& d) const
    {
        if (d.offLatency <= 0) return 0;
        return d.offLatencyRate > 0 ? juce::roundToInt (d.offLatency * rate / d.offLatencyRate) : d.offLatency;
    }   // the Note Splitters' outputs, by (device, output) (decision 0083)
    std::map<Id, juce::AudioPluginInstance*> araOwner;   // the instance each ARA session was started on
    bool rendering = false;   // exporting: the Cue track is left out (decision 0051)
    std::atomic<int> startedInputs { 0 };   // the input channels of the device last started
    Id renderOnlyTrack = 0;   // rendering one track alone (decision 0080)
    std::map<Id, std::unique_ptr<AraSession>> araSessions;
    std::set<Id> araTracks;
    Node::Ptr audioIn, audioOut, midiIn, metronomeNode;
    std::vector<std::pair<Node::Ptr, juce::uint32>> graveyard;   // let go of a second later
    juce::String shape;   // the wiring last built

    double rate = 48000;
    int blockSize = 512;

    std::atomic<bool> playing { false }, recording { false };
    static constexpr double noJump = -1.0e9;
    std::atomic<double> pos { 0 }, seekTo { noJump };
    std::atomic<bool> loopOn { false };
    std::atomic<double> loopStart { 0 }, loopEnd { 0 };
    double countInSeconds = 0;
    std::atomic<bool> waitingForLink { false };
    Swap<TempoMap> tempo;
    TempoMap tempoCopy;
    Id liveTrack = 0;

    // The audio thread's own reading of the transport, for plug-ins.
    juce::AudioPlayHead::PositionInfo info;
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override { return info; }

    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float value) override;
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override;
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index) override;
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override;
    void touched (juce::AudioProcessor*, int index, float value, int gesture);
    void timerCallback() override;

    Node::Ptr nodeForDevice (const Device&, Id version, juce::String& error);
    static juce::AudioPluginInstance* pluginOf (const Node::Ptr&);
    std::map<Id, std::vector<Id>> trackDevices;
    std::map<Id, juce::int64> lastBusy;
    std::map<Id, double> trackLoad;
    juce::int64 lastLoadTicks = 0;
    int loadCountdown = 0;
    std::atomic<int> late { 0 };
    int dropoutBase = 0;
    void measureLoad();
    std::shared_ptr<bool> aliveToken = std::make_shared<bool> (true);   // for messages sent to the message thread
    Id deviceIdOf (juce::AudioProcessor*) const;
    void bury (Node::Ptr);
};

} // namespace kal
