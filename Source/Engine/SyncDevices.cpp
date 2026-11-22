#include "SyncDevices.h"

#include "NetworkMidi.h"
#include "TrackProcessors.h"

#include <ableton/Link.hpp>

namespace kal
{
using namespace juce;

struct SyncDevices::LinkState
{
    ableton::Link session { 120.0 };
    double lastSessionTempo = 0, lastOurTempo = 0;
    bool lastSessionPlaying = false, lastOurPlaying = false;
    double lastPhase = -1;
    bool waiting = false;
};

SyncDevices::SyncDevices()
{
    scratch.ensureSize (4096);
}

SyncDevices::~SyncDevices()
{
    const ScopedLock sl (outputLock);
    outputs.clear();
    if (link) link->session.enable (false);
}

void SyncDevices::configure (const ControlSettings& s)
{
    const ScopedLock sl (outputLock);
    settings = s;
    outputs.clear();
    toNetwork = false;
    for (auto& id : s.clockOut)
        if (id == networkmidi::portId) toNetwork = true;   // Kalibre's own session (0129)
        else if (auto out = MidiOutput::openDevice (String (id)))
        {
            out->startBackgroundThread();
            outputs.push_back (std::move (out));
        }
    followId = String (s.followClock);
    follower.reset();
    mtcId = String (s.followMtc);
    mtc.reset();
    lastMtcMs = -1.0;
    if (s.link && ! link) link = std::make_unique<LinkState>();
    if (link)
    {
        link->session.enable (s.link);
        link->session.enableStartStopSync (s.link && s.linkStartStop);
    }
    wasPlaying = false;
}

bool SyncDevices::linkOn() const { return link && link->session.isEnabled(); }
int SyncDevices::linkPeers() const { return link ? (int) link->session.numPeers() : 0; }

void SyncDevices::command (Command::Kind kind, double value)
{
    MessageManager::callAsync ([this, kind, value] { if (onCommand) onCommand ({ kind, value }); });
}

//==============================================================================
void SyncDevices::block (const Clock& c, const TempoMap& tempo, double heardAtMs, int quantumBeats)
{
    if (link && link->session.isEnabled()) runLink (c, tempo, quantumBeats);
    // Chasing timecode (decision 0043): when it stops coming, Kalibre stops.
    if (const double last = lastMtcMs.load(); last > 0 && c.playing && Time::getMillisecondCounterHiRes() - last > 300.0)
    {
        lastMtcMs = -1.0;
        command (Command::Kind::Stop, 0);
    }

    const ScopedTryLock sl (outputLock);
    if (! sl.isLocked() || (outputs.empty() && ! toNetwork))
    {
        wasPlaying = c.playing;
        return;
    }
    scratch.clear();
    const double rate = c.rate;
    auto ppqAt = [&] (double seconds) { return tempo.ticksAt (seconds) / PPQ; };
    auto sendPosition = [&] (int at, double seconds)
    {
        const int spp = sync::songPosition (ppqAt (seconds));
        scratch.addEvent (MidiMessage::songPositionPointer (spp), at);
        if (settings.sendMtc)
        {
            const auto ff = sync::fullFrame (seconds, settings.mtcRate);
            scratch.addEvent (ff.data(), (int) ff.size(), at);
            nextQuarterFrame = (std::int64_t) std::ceil (seconds * 4.0 * sync::framesPerSecond (settings.mtcRate));
        }
    };

    // MIDI Machine Control out: other machines start, stop and locate with Kalibre.
    auto sendMmc = [&] (const std::vector<std::uint8_t>& bytes) { scratch.addEvent (bytes.data(), (int) bytes.size(), 0); };
    if (settings.sendMmc)
    {
        if (c.playing && (! wasPlaying || c.jumped))
        {
            sendMmc (sync::mmc::locate (c.start, settings.mtcRate));
            sendMmc (sync::mmc::command (sync::mmc::DeferredPlay));
        }
        else if (! c.playing && wasPlaying) sendMmc (sync::mmc::command (sync::mmc::Stop));
    }
    if (c.playing && (! wasPlaying || c.jumped))
    {
        if (wasPlaying) scratch.addEvent (MidiMessage::midiStop(), 0);
        sendPosition (0, c.start);
        scratch.addEvent (c.start <= 0.0001 ? MidiMessage::midiStart() : MidiMessage::midiContinue(), 0);
    }
    else if (! c.playing && wasPlaying)
        scratch.addEvent (MidiMessage::midiStop(), 0);

    if (c.playing)
    {
        // Each stretch of the block: up to the loop's wrap, and after it.
        auto stretch = [&] (double from, double to, int offset)
        {
            if (settings.sendClock)
            {
                const auto pulses = sync::clockPulses (ppqAt (from), ppqAt (to));
                for (int k = 0; k < pulses.count; ++k)
                {
                    const double ppq = (double) (pulses.first + k) / sync::pulsesPerQuarter;
                    const double at = tempo.secondsAt ((Tick) std::llround (ppq * PPQ)) - from;
                    scratch.addEvent (MidiMessage::midiClock(), offset + jlimit (0, c.samples - 1, (int) (at * rate)));
                }
            }
            if (settings.sendMtc)
            {
                const double fps = sync::framesPerSecond (settings.mtcRate);
                if (nextQuarterFrame < (std::int64_t) std::floor (from * 4.0 * fps)) nextQuarterFrame = (std::int64_t) std::ceil (from * 4.0 * fps);
                while (sync::quarterFrameTime (nextQuarterFrame, settings.mtcRate) < to)
                {
                    const double at = sync::quarterFrameTime (nextQuarterFrame, settings.mtcRate) - from;
                    scratch.addEvent (MidiMessage::quarterFrame ((nextQuarterFrame % 8), sync::quarterFrame (nextQuarterFrame, settings.mtcRate) & 15),
                                      offset + jlimit (0, c.samples - 1, (int) (at * rate)));
                    ++nextQuarterFrame;
                }
            }
        };
        if (c.wrapAt >= 0)
        {
            stretch (c.start, c.start + c.wrapAt / rate, 0);
            // Round the loop: stop, the new position, carry on.
            scratch.addEvent (MidiMessage::midiStop(), c.wrapAt);
            sendPosition (c.wrapAt, c.wrapTo);
            scratch.addEvent (MidiMessage::midiContinue(), c.wrapAt);
            stretch (c.wrapTo, c.wrapTo + (c.samples - c.wrapAt) / rate, c.wrapAt);
        }
        else
            stretch (c.start, c.start + c.samples / rate, 0);
    }
    wasPlaying = c.playing;
    send (heardAtMs, rate);
}

void SyncDevices::send (double heardAtMs, double rate)
{
    if (scratch.isEmpty()) return;
    for (auto& out : outputs) out->sendBlockOfMessages (scratch, heardAtMs, rate);
    if (toNetwork)
        for (const auto m : scratch) networkmidi::sendAt (m.getMessage(), heardAtMs + 1000.0 * m.samplePosition / rate);
}

//==============================================================================
void SyncDevices::input (const String& device, const MidiMessage& m)
{
    // MMC from anywhere, when it's obeyed.
    if (m.isSysEx() && settings.followMmc)
    {
        const auto mm = sync::mmc::decode (m.getRawData(), m.getRawDataSize());
        switch (mm.command)
        {
            case sync::mmc::Play: case sync::mmc::DeferredPlay: command (Command::Kind::Play, -1.0); return;
            case sync::mmc::Stop: case sync::mmc::Pause: command (Command::Kind::Stop, 0); return;
            case sync::mmc::Locate: command (Command::Kind::LocateSeconds, mm.seconds); return;
            case sync::mmc::RecordStrobe: command (Command::Kind::Record, 0); return;
            default: break;
        }
    }
    // Timecode to chase.
    if (mtcId.isNotEmpty() && (device.isEmpty() || device == mtcId))
    {
        if (m.isQuarterFrame())
        {
            if (mtc.quarterFrame ((std::uint8_t) ((m.getQuarterFrameSequenceNumber() << 4) | m.getQuarterFrameValue())))
            {
                lastMtcMs = Time::getMillisecondCounterHiRes();
                command (Command::Kind::Chase, mtc.seconds());
            }
            return;
        }
        if (m.isFullFrame() && mtc.fullFrame (m.getRawData(), m.getRawDataSize()))
        {
            command (Command::Kind::LocateSeconds, mtc.seconds());
            return;
        }
    }
    if (followId.isEmpty() || (device.isNotEmpty() && device != followId)) return;
    if (m.isMidiClock())
    {
        follower.pulse (Time::getMillisecondCounterHiRes() / 1000.0);
        // Once a beat, the tempo is passed on when it's steady.
        if (++pulsesSinceTempo >= sync::pulsesPerQuarter && follower.steady())
        {
            pulsesSinceTempo = 0;
            command (Command::Kind::Tempo, std::round (follower.bpm() * 100.0) / 100.0);
        }
    }
    else if (m.isMidiStart()) { follower.start(); command (Command::Kind::Play, 0.0); }
    else if (m.isMidiContinue()) command (Command::Kind::Play, -1.0);
    else if (m.isMidiStop()) command (Command::Kind::Stop, 0);
    else if (m.isSongPositionPointer())
    {
        follower.songPosition (m.getSongPositionPointerMidiBeat());
        command (Command::Kind::Locate, sync::ppqOfSongPosition (m.getSongPositionPointerMidiBeat()));
    }
}

//==============================================================================
void SyncDevices::runLink (const Clock& c, const TempoMap&, int quantumBeats)
{
    auto& l = *link;
    auto state = l.session.captureAudioSessionState();
    const auto now = l.session.clock().micros();
    bool commit = false;

    // Tempo: whoever changed it last wins.
    const double session = state.tempo();
    if (std::abs (c.bpm - l.lastOurTempo) > 0.005)
    {
        l.lastOurTempo = c.bpm;
        if (std::abs (session - c.bpm) > 0.005) { state.setTempo (c.bpm, now); commit = true; }
        l.lastSessionTempo = c.bpm;
    }
    else if (std::abs (session - l.lastSessionTempo) > 0.005)
    {
        l.lastSessionTempo = session;
        command (Command::Kind::Tempo, std::round (session * 100.0) / 100.0);
    }

    // Start and stop, shared both ways.
    if (settings.linkStartStop)
    {
        if (c.playing != l.lastOurPlaying)
        {
            l.lastOurPlaying = c.playing;
            if (state.isPlaying() != c.playing)
            {
                state.setIsPlayingAndRequestBeatAtTime (c.playing, now, 0.0, quantumBeats);
                commit = true;
            }
            l.lastSessionPlaying = c.playing;
        }
        else if (state.isPlaying() != l.lastSessionPlaying)
        {
            l.lastSessionPlaying = state.isPlaying();
            command (state.isPlaying() ? Command::Kind::Play : Command::Kind::Stop, -1.0);
        }
    }
    if (commit) l.session.commitAudioSessionState (state);
}

bool SyncDevices::holdStartForLink (const TempoMap&, int quantumBeats)
{
    if (! link || ! link->session.isEnabled() || link->session.numPeers() == 0) return false;
    auto& l = *link;
    auto state = l.session.captureAudioSessionState();
    const double phase = state.phaseAtTime (l.session.clock().micros(), quantumBeats);
    // Wait for the session's next bar line: the phase goes back round to zero.
    const bool wrapped = l.lastPhase >= 0 && phase < l.lastPhase;
    l.lastPhase = phase;
    if (wrapped) { l.lastPhase = -1; return false; }
    return true;
}

} // namespace kal
