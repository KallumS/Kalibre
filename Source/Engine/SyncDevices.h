/*
    SyncDevices - MIDI clock, MIDI timecode and Ableton Link (decision 0034).

    Called by the engine once a block on the audio thread (`block`), it sends
    clock pulses, Start, Stop, Continue and Song Position, and MTC quarter
    frames, to the outputs chosen in the browser's Sync tab, timed to the
    sample; it follows a clock coming in; and it keeps Link's session tempo,
    start/stop and bar phase in step with the transport.

    Anything that changes the project (a tempo from Link or a clock, Start
    from another machine) is asked of the window on the message thread
    through `onCommand`.
*/

#pragma once

#include "ControlSettings.h"
#include "Sync.h"
#include "TempoMap.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <functional>
#include <atomic>
#include <memory>

namespace kal
{

struct Clock;

class SyncDevices
{
public:
    SyncDevices();
    ~SyncDevices();

    // Message thread: open the chosen outputs, turn Link on or off.
    void configure (const ControlSettings&);

    // Audio thread. `heardAtMs` is when the block's first sample reaches the speakers (hi-res ms).
    void block (const Clock&, const TempoMap&, double heardAtMs, int quantumBeats);

    // MIDI thread: a clock, Start, Stop, Continue or Song Position arrived.
    void input (const juce::String& device, const juce::MidiMessage&);

    struct Command
    {
        enum class Kind { Play, Stop, Locate, Tempo, Chase, LocateSeconds, Record } kind = Kind::Play;
        double value = 0;   // quarter notes for Locate and Play-from, BPM for Tempo, seconds for Chase and LocateSeconds
    };
    std::function<void (const Command&)> onCommand;

    // Link: playing waits for the next bar of the session when others are in it.
    bool linkOn() const;
    int linkPeers() const;
    bool holdStartForLink (const TempoMap&, int quantumBeats);   // audio thread: true while waiting
    double followedBpm() const { return follower.bpm(); }

private:
    juce::CriticalSection outputLock;
    std::vector<std::unique_ptr<juce::MidiOutput>> outputs;
    ControlSettings settings;
    bool wasPlaying = false;
    std::int64_t nextQuarterFrame = 0;
    juce::MidiBuffer scratch;

    sync::ClockFollower follower;
    sync::MtcReader mtc;
    juce::String mtcId;
    std::atomic<double> lastMtcMs { -1.0 };   // when timecode last came in, while chasing
    juce::String followId;
    int pulsesSinceTempo = 0;

    struct LinkState;
    std::unique_ptr<LinkState> link;

    void send (double heardAtMs, double rate);
    void command (Command::Kind, double value);
    void runLink (const Clock&, const TempoMap&, int quantumBeats);
};

} // namespace kal
