// Kalibre's own network MIDI session, off the Mac (decision 0129): RTP-MIDI
// as the Mac's session speaks it (Core/RtpMidi), on a pair of UDP ports
// (5004 and 5005 unless taken). Other computers invite it; it says yes to
// anyone, answers their clock sync, plays what they send, and sends them
// what Kalibre sends the port (its clock), each at its time.
#include "NetworkMidi.h"

#include "RtpMidi.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace kal::networkmidi
{
using namespace juce;

namespace detail
{
// Announced on the network as an AppleMIDI session (_apple-midi._udp), where the system can.
bool announce (const String& name, int port);
void withdraw();
} // namespace detail

namespace
{
std::uint64_t now100us() { return (std::uint64_t) (Time::getMillisecondCounterHiRes() * 10.0); }

class Session : private Thread
{
public:
    Session() : Thread ("Kalibre network MIDI") {}
    ~Session() override { close(); }

    bool open (int first)
    {
        close();
        for (int p = first > 0 ? first : 5004; p < (first > 0 ? first + 2 : 5100); p += 2)
        {
            auto c = std::make_unique<DatagramSocket> (false), d = std::make_unique<DatagramSocket> (false);
            if (c->bindToPort (p) && d->bindToPort (p + 1))
            {
                control = std::move (c);
                data = std::move (d);
                controlPort = p;
                break;
            }
        }
        if (! control) return false;
        ssrc = (std::uint32_t) Random::getSystemRandom().nextInt64();
        name = "Kalibre on " + SystemStats::getComputerName();
        peerList.clear();
        peerCount = 0;
        fifo.reset();
        on = true;
        startThread (Priority::high);
        detail::announce (name, controlPort);
        return true;
    }

    void close()
    {
        if (! control) return;
        detail::withdraw();
        // Goodbye to each, so their lists don't keep a session that's gone.
        for (auto& p : peerList)
        {
            rtpmidi::Session by;
            by.command = rtpmidi::Command::End;
            by.ssrc = ssrc;
            const auto b = rtpmidi::write (by);
            control->write (p.address, p.controlPort, b.data(), (int) b.size());
        }
        on = false;
        stopThread (2000);
        control.reset();
        data.reset();
        controlPort = 0;
        peerList.clear();
        peerCount = 0;
    }

    // Any thread: queued for the session's thread, which sends it when it's due.
    void push (const MidiMessage& m, double whenMs)
    {
        if (! on || m.getRawDataSize() > 3 || m.getRawDataSize() < 1) return;
        const auto scope = fifo.write (1);
        if (scope.blockSize1 + scope.blockSize2 == 0) return;   // full: dropped rather than waited for
        auto& o = queue[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)];
        o.size = m.getRawDataSize();
        std::memcpy (o.bytes, m.getRawData(), (size_t) o.size);
        o.when = whenMs;
    }

    std::function<void (const MidiMessage&)> receiver;
    std::mutex receiverLock;
    std::atomic<int> peerCount { 0 };
    std::atomic<bool> on { false };
    int controlPort = 0;
    String name;

private:
    struct Peer
    {
        std::uint32_t ssrc = 0;
        String address;
        int controlPort = 0, dataPort = 0;
    };
    struct Outgoing { std::uint8_t bytes[3] {}; int size = 0; double when = 0; };

    std::unique_ptr<DatagramSocket> control, data;
    std::uint32_t ssrc = 0;
    std::uint16_t sequence = 0;
    std::vector<Peer> peerList;   // the session's thread's
    std::array<Outgoing, 2048> queue;
    AbstractFifo fifo { 2048 };
    std::vector<rtpmidi::Bytes> due;

    void run() override
    {
        std::array<std::uint8_t, 2048> buffer {};
        due.reserve (256);
        while (! threadShouldExit())
        {
            for (auto* socket : { control.get(), data.get() })
                if (socket != nullptr && socket->waitUntilReady (true, 1) == 1)
                {
                    String from;
                    int fromPort = 0;
                    const int n = socket->read (buffer.data(), (int) buffer.size(), false, from, fromPort);
                    if (n > 0) handle (socket == data.get(), rtpmidi::Bytes (buffer.begin(), buffer.begin() + n), from, fromPort);
                }
            sendDue();
        }
    }

    Peer& peerFor (std::uint32_t id)
    {
        for (auto& p : peerList) if (p.ssrc == id) return p;
        peerList.push_back ({ id, {}, 0, 0 });
        return peerList.back();
    }

    void reply (DatagramSocket& socket, const rtpmidi::Session& s, const String& to, int toPort)
    {
        const auto b = rtpmidi::write (s);
        socket.write (to, toPort, b.data(), (int) b.size());
    }

    void handle (bool onData, const rtpmidi::Bytes& b, const String& from, int fromPort)
    {
        auto& socket = onData ? *data : *control;
        if (const auto s = rtpmidi::readSession (b))
        {
            switch (s->command)
            {
                case rtpmidi::Command::Invitation:
                {
                    // Anyone on the network may join, as the Mac's session lets them.
                    rtpmidi::Session ok;
                    ok.command = rtpmidi::Command::Accepted;
                    ok.token = s->token;
                    ok.ssrc = ssrc;
                    ok.name = name.toStdString();
                    reply (socket, ok, from, fromPort);
                    auto& p = peerFor (s->ssrc);
                    p.address = from;
                    (onData ? p.dataPort : p.controlPort) = fromPort;
                    break;
                }
                case rtpmidi::Command::End:
                    peerList.erase (std::remove_if (peerList.begin(), peerList.end(), [&] (const Peer& p) { return p.ssrc == s->ssrc; }), peerList.end());
                    break;
                case rtpmidi::Command::Sync:
                    if (const auto r = rtpmidi::syncReply (*s, ssrc, now100us())) reply (socket, *r, from, fromPort);
                    break;
                default: break;
            }
            int connected = 0;
            for (auto& p : peerList) connected += p.dataPort != 0 ? 1 : 0;
            peerCount = connected;
            return;
        }
        if (! onData) return;
        const auto midi = rtpmidi::readMidi (b);
        if (! midi) return;
        const std::lock_guard<std::mutex> l (receiverLock);
        if (! receiver) return;
        for (auto& m : midi->messages)
            receiver (MidiMessage (m.data(), (int) m.size(), Time::getMillisecondCounterHiRes() * 0.001));
    }

    // What's due now, in one packet to each computer connected.
    void sendDue()
    {
        const double now = Time::getMillisecondCounterHiRes();
        due.clear();
        while (fifo.getNumReady() > 0)
        {
            int start1, size1, start2, size2;
            fifo.prepareToRead (1, start1, size1, start2, size2);
            const auto& o = queue[(size_t) (size1 > 0 ? start1 : start2)];
            if (o.when > now + 0.5) break;   // not yet
            due.push_back (rtpmidi::Bytes (o.bytes, o.bytes + o.size));
            fifo.finishedRead (1);
            if (due.size() >= 200) break;
        }
        if (due.empty() || ! data) return;
        const auto packet = rtpmidi::writeMidi (due, sequence++, (std::uint32_t) now100us(), ssrc);
        for (auto& p : peerList)
            if (p.dataPort != 0) data->write (p.address, p.dataPort, packet.data(), (int) packet.size());
    }
};

Session& session()
{
    static Session s;
    return s;
}
} // namespace

bool available() { return true; }

bool setEnabledOn (bool on, int firstPort)
{
    auto& s = session();
    if (! on) { s.close(); return true; }
    if (s.on && (firstPort == 0 || s.controlPort == firstPort)) return true;
    return s.open (firstPort);
}

bool setEnabled (bool on) { return setEnabledOn (on, 0); }
juce::String sessionName() { return session().on ? session().name : String(); }
bool ownPort() { return session().on.load(); }
juce::String portName() { return "Network Session (Kalibre)"; }
int peers() { return session().peerCount.load(); }
int port() { return session().on ? session().controlPort : 0; }

void setReceiver (std::function<void (const juce::MidiMessage&)> f)
{
    auto& s = session();
    const std::lock_guard<std::mutex> l (s.receiverLock);
    s.receiver = std::move (f);
}

void sendAt (const juce::MidiMessage& m, double whenMs) { session().push (m, whenMs); }

#if ! JUCE_WINDOWS
// Linux: no system service to announce it with; other computers connect by its address.
namespace detail
{
bool announce (const String&, int) { return false; }
void withdraw() {}
} // namespace detail
#endif

} // namespace kal::networkmidi
