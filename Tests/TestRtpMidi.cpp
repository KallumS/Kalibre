#include "Check.h"
#include "RtpMidi.h"

using namespace kal;
using namespace kal::rtpmidi;

TEST ("rtp midi: an invitation and its answer, as Apple's session writes them, come back as written (0129)")
{
    Session in;
    in.command = Command::Invitation;
    in.token = 0x11223344;
    in.ssrc = 0xCAFEBABE;
    in.name = "iPad";
    const auto bytes = write (in);
    // FF FF 'I' 'N', version 2, token, ssrc, the name and a zero.
    CHECK_EQ (bytes.size(), (size_t) (16 + 4 + 1));
    CHECK (bytes[2] == 'I' && bytes[3] == 'N' && bytes[7] == 2);
    CHECK (bytes[8] == 0x11 && bytes[15] == 0xBE && bytes.back() == 0);
    const auto back = readSession (bytes);
    CHECK (back.has_value());
    if (! back) return;
    CHECK (back->command == Command::Invitation);
    CHECK_EQ (back->token, (std::uint32_t) 0x11223344);
    CHECK_EQ (back->ssrc, (std::uint32_t) 0xCAFEBABE);
    CHECK_EQ (back->name, std::string ("iPad"));
    Session by;
    by.command = Command::End;
    by.ssrc = 7;
    CHECK (readSession (write (by))->command == Command::End);
    CHECK (! readSession (Bytes { 0xFF, 0xFF, 'Z', 'Z' }).has_value());
    CHECK (! isSession (Bytes { 0x80, 0x61 }));
}

TEST ("rtp midi: clock sync is answered with our time, count 0 with 1 and 1 with 2; 2 needs no answer (0129)")
{
    Session ck;
    ck.command = Command::Sync;
    ck.ssrc = 99;
    ck.count = 0;
    ck.timestamps[0] = 123456789;
    const auto read = readSession (write (ck));
    CHECK (read.has_value() && read->command == Command::Sync && read->timestamps[0] == 123456789);
    const auto reply = syncReply (*read, 5, 1000);
    CHECK (reply.has_value());
    if (reply)
    {
        CHECK_EQ ((int) reply->count, 1);
        CHECK_EQ (reply->ssrc, (std::uint32_t) 5);
        CHECK_EQ (reply->timestamps[0], (std::uint64_t) 123456789);
        CHECK_EQ (reply->timestamps[1], (std::uint64_t) 1000);
        CHECK_EQ (write (*reply).size(), (size_t) 36);
    }
    ck.count = 2;
    CHECK (! syncReply (ck, 5, 1000).has_value());
}

TEST ("rtp midi: MIDI in an RTP packet: running status, delta times, real time between, System Exclusive (0129)")
{
    // A packet as an iPad sends it: note on, then (running status, a delta time before it) note off,
    // a clock tick, a program change, and a SysEx.
    Bytes list { 0x90, 60, 100,   0x05, 60, 0,   0x00, 0xF8,   0x81, 0x00, 0xC1, 5,   0x00, 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 };
    Bytes p { 0x80, 0x61, 0x00, 0x2A, 0, 0, 0, 1, 0x12, 0x34, 0x56, 0x78 };
    p.push_back ((std::uint8_t) (0x80 | (list.size() >> 8)));
    p.push_back ((std::uint8_t) list.size());
    p.insert (p.end(), list.begin(), list.end());
    const auto r = readMidi (p);
    CHECK (r.has_value());
    if (! r) return;
    CHECK_EQ (r->sequence, (std::uint16_t) 42);
    CHECK_EQ (r->ssrc, (std::uint32_t) 0x12345678);
    CHECK_EQ (r->messages.size(), (size_t) 5);
    if (r->messages.size() != 5) return;
    CHECK (r->messages[0] == (Bytes { 0x90, 60, 100 }));
    CHECK (r->messages[1] == (Bytes { 0x90, 60, 0 }));
    CHECK (r->messages[2] == (Bytes { 0xF8 }));
    CHECK (r->messages[3] == (Bytes { 0xC1, 5 }));
    CHECK (r->messages[4] == (Bytes { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 }));
    // Not RTP-MIDI: nothing.
    CHECK (! readMidi (Bytes { 0xFF, 0xFF, 'C', 'K' }).has_value());
}

TEST ("rtp midi: what Kalibre sends reads back as it was, a short list or a long one (0129)")
{
    const std::vector<Bytes> few { { 0xF8 }, { 0x90, 64, 90 } };
    const auto a = readMidi (writeMidi (few, 7, 1000, 0xABCDEF01));
    CHECK (a.has_value() && a->messages == few && a->sequence == 7 && a->ssrc == 0xABCDEF01);
    std::vector<Bytes> many;
    for (int i = 0; i < 40; ++i) many.push_back ({ 0xB0, (std::uint8_t) i, 64 });   // past 15 bytes: the long length
    const auto b = readMidi (writeMidi (many, 8, 2000, 1));
    CHECK (b.has_value() && b->messages == many);
    CHECK_EQ (dataBytesAfter (0xC3), 1);
    CHECK_EQ (dataBytesAfter (0xE0), 2);
    CHECK_EQ (dataBytesAfter (0xF0), -1);
    CHECK_EQ (dataBytesAfter (0xF8), 0);
}
