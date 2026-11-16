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
    Fader* fader (Id track) const;
    TrackSource* source (Id track) const;
    int latencySamples();
    double sampleRate() const { return rate; }

    // A device's saved state, as the project keeps it (base64).
    std::string stateOf (Id device) const;
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
        double tail = 2.0;
        double sampleRate = 48000;
        int bitDepth = 24;
        bool normalise = false;
        double loudnessTarget = 0;          // LUFS (e.g. -14); 0 leaves the level alone
        double truePeakCeiling = -1.0;      // dBTP the loudness gain may not push past
        bool dither = true;
        juce::String format = "wav";        // wav, aiff, flac
    };
    // Renders the project as it would play (not in real time) into a file.
    // The device is paused while it works. `progress` returning false cancels.
    bool render (const Project&, const juce::File& folder, const juce::File& out, const RenderOptions&,
                 std::function<bool (float)> progress, juce::String& error);

    // Offline effects on one audio clip: its audio through a chain, into a new file.
    bool renderOfflineFx (const Clip&, const juce::File& source, const juce::File& out, juce::String& error);

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
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorPlayer player;

    using NodeID = juce::AudioProcessorGraph::NodeID;
    using Node = juce::AudioProcessorGraph::Node;

    struct TrackNodes
    {
        Node::Ptr source, fader;
        std::map<Id, Node::Ptr> sends;
    };
    std::map<Id, TrackNodes> tracks;
    std::map<Id, Node::Ptr> deviceNodes;
    std::map<Id, std::string> deviceKeys;   // what each node was made from, to notice a swap
    std::map<Id, std::unique_ptr<AraSession>> araSessions;
    std::set<Id> araTracks;
    Node::Ptr audioIn, audioOut, midiIn, metronomeNode;
    std::vector<std::pair<Node::Ptr, juce::uint32>> graveyard;   // let go of a second later
    juce::String shape;   // the wiring last built

    double rate = 48000;
    int blockSize = 512;

    std::atomic<bool> playing { false }, recording { false };
    std::atomic<double> pos { 0 }, seekTo { -1 };
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

    Node::Ptr nodeForDevice (const Device&, juce::String& error);
    Id deviceIdOf (juce::AudioProcessor*) const;
    void bury (Node::Ptr);
};

} // namespace kal
