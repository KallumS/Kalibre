// Kalibre's own network MIDI session, off the Mac (decision 0129): another
// computer invites it, syncs its clock, plays into it, and hears its clock;
// spoken here as an iPad or a Mac's session would, over the loopback.
#include "Check.h"
#include "Engine.h"
#include "NetworkMidi.h"
#include "PluginManager.h"
#include "RtpMidi.h"

using namespace kal;
using namespace juce;

#if ! JUCE_MAC
namespace
{
rtpmidi::Bytes ask (DatagramSocket& s, int port, const rtpmidi::Bytes& b)
{
    s.write ("127.0.0.1", port, b.data(), (int) b.size());
    rtpmidi::Bytes reply (2048);
    if (s.waitUntilReady (true, 2000) != 1) return {};
    String from;
    int fromPort = 0;
    const int n = s.read (reply.data(), (int) reply.size(), false, from, fromPort);
    reply.resize ((size_t) juce::jmax (0, n));
    return reply;
}
} // namespace

TEST ("network midi: another computer joins Kalibre's own session, syncs its clock, plays into it, hears its clock, and leaves (0129)")
{
    PluginManager plugins;
    Engine engine (plugins);
    CHECK (networkmidi::available());
    CHECK (networkmidi::setEnabledOn (true, 21004));
    CHECK (networkmidi::ownPort());
    CHECK_EQ (networkmidi::port(), 21004);
    CHECK (networkmidi::sessionName().startsWith ("Kalibre on "));
    DatagramSocket control (false), data (false);
    CHECK (control.bindToPort (0) && data.bindToPort (0));

    // Invited on both ports, as Apple's session does: OK each time, with Kalibre's name.
    rtpmidi::Session in;
    in.command = rtpmidi::Command::Invitation;
    in.token = 77;
    in.ssrc = 0x1234;
    in.name = "Test iPad";
    for (auto* s : { &control, &data })
    {
        const auto ok = rtpmidi::readSession (ask (*s, s == &control ? 21004 : 21005, rtpmidi::write (in)));
        CHECK (ok.has_value());
        if (! ok) return;
        CHECK (ok->command == rtpmidi::Command::Accepted);
        CHECK_EQ (ok->token, (std::uint32_t) 77);
        CHECK (String (ok->name).startsWith ("Kalibre on "));
    }
    CHECK_EQ (networkmidi::peers(), 1);

    // Clock sync: our count 0 answered with count 1, our time kept in it.
    rtpmidi::Session ck;
    ck.command = rtpmidi::Command::Sync;
    ck.ssrc = 0x1234;
    ck.timestamps[0] = 42;
    const auto ck1 = rtpmidi::readSession (ask (data, 21005, rtpmidi::write (ck)));
    CHECK (ck1.has_value() && ck1->command == rtpmidi::Command::Sync && ck1->count == 1 && ck1->timestamps[0] == 42 && ck1->timestamps[1] > 0);

    // A note played in reaches the engine as a note from a keyboard.
    const auto before = engine.notesPlayedIn.load();
    const auto note = rtpmidi::writeMidi ({ { 0x90, 60, 100 } }, 1, 0, 0x1234);
    data.write ("127.0.0.1", 21005, note.data(), (int) note.size());
    for (int i = 0; i < 100 && engine.notesPlayedIn.load() == before; ++i) Thread::sleep (10);
    CHECK_EQ (engine.notesPlayedIn.load(), before + 1);

    // What Kalibre sends the port (its clock) arrives at the computer's data port, when it's due.
    networkmidi::sendAt (MidiMessage::midiClock(), Time::getMillisecondCounterHiRes());
    networkmidi::sendAt (MidiMessage::midiStart(), Time::getMillisecondCounterHiRes() + 20);
    std::vector<rtpmidi::Bytes> heard;
    for (int i = 0; i < 20 && heard.size() < 2 && data.waitUntilReady (true, 200) == 1; ++i)
    {
        rtpmidi::Bytes b (2048);
        String from;
        int fromPort = 0;
        b.resize ((size_t) jmax (0, data.read (b.data(), (int) b.size(), false, from, fromPort)));
        if (const auto r = rtpmidi::readMidi (b)) heard.insert (heard.end(), r->messages.begin(), r->messages.end());
    }
    CHECK_EQ (heard.size(), (size_t) 2);
    if (heard.size() == 2) { CHECK (heard[0] == rtpmidi::Bytes { 0xF8 }); CHECK (heard[1] == rtpmidi::Bytes { 0xFA }); }

    // Gone: BY, and no one is connected.
    rtpmidi::Session by;
    by.command = rtpmidi::Command::End;
    by.ssrc = 0x1234;
    const auto bye = rtpmidi::write (by);
    control.write ("127.0.0.1", 21004, bye.data(), (int) bye.size());
    for (int i = 0; i < 100 && networkmidi::peers() > 0; ++i) Thread::sleep (10);
    CHECK_EQ (networkmidi::peers(), 0);
    CHECK (networkmidi::setEnabled (false));
    CHECK (! networkmidi::ownPort());
    CHECK_EQ (networkmidi::port(), 0);
}
#endif
