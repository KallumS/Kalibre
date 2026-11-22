/*
    RtpMidi - network MIDI as Apple's session speaks it (RTP-MIDI, RFC 6295,
    with AppleMIDI's session protocol), for Kalibre's own network MIDI session
    off the Mac (decision 0129). The Mac has its session built in (0043); an
    iPad or a Mac connects to Kalibre on Windows the same way.

    The packets only: a session's invitation (IN), its answer (OK, NO), its
    end (BY), clock sync (CK), and MIDI inside an RTP packet. The sockets are
    the engine's (NetworkMidiRtp.cpp).

    No JUCE in here.
*/

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kal::rtpmidi
{

using Bytes = std::vector<std::uint8_t>;

// AppleMIDI's session commands: FF FF and two letters.
enum class Command { Invitation, Accepted, Rejected, End, Sync, Feedback };

struct Session
{
    Command command = Command::Invitation;
    std::uint32_t version = 2, token = 0, ssrc = 0;
    std::string name;                        // IN, OK: who it is ("Kalibre")
    // CK: three timestamps in 100 microsecond units; count says how many are filled (0, 1, 2).
    std::uint8_t count = 0;
    std::uint64_t timestamps[3] {};
    std::uint32_t sequence = 0;              // RS
};

bool isSession (const Bytes&);
std::optional<Session> readSession (const Bytes&);
Bytes write (const Session&);

// The answer to a clock sync: count 0 is answered with count 1 (our time
// added), count 1 with count 2; count 2 needs none.
std::optional<Session> syncReply (const Session& received, std::uint32_t ourSsrc, std::uint64_t now);

// MIDI in an RTP packet: each message's bytes, in order. Delta times are
// read past (each message is played as it comes); running status and
// System Exclusive are read whole. Empty when it isn't RTP-MIDI.
struct Received
{
    std::uint32_t ssrc = 0;
    std::uint16_t sequence = 0;
    std::vector<Bytes> messages;
};
std::optional<Received> readMidi (const Bytes&);

// Messages in an RTP packet, to send now: no journal, delta time 0 between them.
Bytes writeMidi (const std::vector<Bytes>& messages, std::uint16_t sequence, std::uint32_t timestamp, std::uint32_t ssrc);

// How many data bytes follow a status byte; -1 for System Exclusive (to F7).
int dataBytesAfter (std::uint8_t status);

} // namespace kal::rtpmidi
