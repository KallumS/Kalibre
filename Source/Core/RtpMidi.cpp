#include "RtpMidi.h"

#include <algorithm>
#include <cstring>

namespace kal::rtpmidi
{
namespace
{
void put32 (Bytes& b, std::uint32_t v) { for (int s = 24; s >= 0; s -= 8) b.push_back ((std::uint8_t) (v >> s)); }
void put64 (Bytes& b, std::uint64_t v) { for (int s = 56; s >= 0; s -= 8) b.push_back ((std::uint8_t) (v >> s)); }
std::uint32_t get32 (const Bytes& b, size_t at) { std::uint32_t v = 0; for (size_t i = 0; i < 4; ++i) v = (v << 8) | b[at + i]; return v; }
std::uint64_t get64 (const Bytes& b, size_t at) { std::uint64_t v = 0; for (size_t i = 0; i < 8; ++i) v = (v << 8) | b[at + i]; return v; }

const char* letters (Command c)
{
    switch (c)
    {
        case Command::Invitation: return "IN";
        case Command::Accepted:   return "OK";
        case Command::Rejected:   return "NO";
        case Command::End:        return "BY";
        case Command::Sync:       return "CK";
        case Command::Feedback:   return "RS";
    }
    return "??";
}
} // namespace

bool isSession (const Bytes& b) { return b.size() >= 4 && b[0] == 0xFF && b[1] == 0xFF; }

std::optional<Session> readSession (const Bytes& b)
{
    if (! isSession (b)) return std::nullopt;
    Session s;
    const std::string c { (char) b[2], (char) b[3] };
    if (c == "IN" || c == "OK" || c == "NO" || c == "BY")
    {
        s.command = c == "IN" ? Command::Invitation : c == "OK" ? Command::Accepted : c == "NO" ? Command::Rejected : Command::End;
        if (b.size() < 16) return std::nullopt;
        s.version = get32 (b, 4);
        s.token = get32 (b, 8);
        s.ssrc = get32 (b, 12);
        for (size_t i = 16; i < b.size() && b[i] != 0; ++i) s.name += (char) b[i];
        return s;
    }
    if (c == "CK")
    {
        if (b.size() < 36) return std::nullopt;
        s.command = Command::Sync;
        s.ssrc = get32 (b, 4);
        s.count = b[8];
        for (size_t i = 0; i < 3; ++i) s.timestamps[i] = get64 (b, 12 + i * 8);
        return s;
    }
    if (c == "RS")
    {
        if (b.size() < 12) return std::nullopt;
        s.command = Command::Feedback;
        s.ssrc = get32 (b, 4);
        s.sequence = get32 (b, 8);
        return s;
    }
    return std::nullopt;
}

Bytes write (const Session& s)
{
    Bytes b { 0xFF, 0xFF, (std::uint8_t) letters (s.command)[0], (std::uint8_t) letters (s.command)[1] };
    switch (s.command)
    {
        case Command::Invitation: case Command::Accepted: case Command::Rejected: case Command::End:
            put32 (b, s.version);
            put32 (b, s.token);
            put32 (b, s.ssrc);
            if (s.command != Command::End)
            {
                for (char ch : s.name) b.push_back ((std::uint8_t) ch);
                b.push_back (0);
            }
            break;
        case Command::Sync:
            put32 (b, s.ssrc);
            b.push_back (s.count);
            b.push_back (0); b.push_back (0); b.push_back (0);
            for (auto t : s.timestamps) put64 (b, t);
            break;
        case Command::Feedback:
            put32 (b, s.ssrc);
            put32 (b, s.sequence);
            break;
    }
    return b;
}

std::optional<Session> syncReply (const Session& r, std::uint32_t ourSsrc, std::uint64_t now)
{
    if (r.command != Command::Sync || r.count >= 2) return std::nullopt;
    Session s = r;
    s.ssrc = ourSsrc;
    s.count = (std::uint8_t) (r.count + 1);
    s.timestamps[s.count] = now;
    return s;
}

int dataBytesAfter (std::uint8_t status)
{
    if (status < 0xF0)
    {
        const auto kind = status & 0xF0;
        return kind == 0xC0 || kind == 0xD0 ? 1 : 2;
    }
    switch (status)
    {
        case 0xF0: return -1;
        case 0xF1: case 0xF3: return 1;
        case 0xF2: return 2;
        default: return 0;   // tune request, real time
    }
}

std::optional<Received> readMidi (const Bytes& b)
{
    if (b.size() < 13 || (b[0] >> 6) != 2 || (b[1] & 0x7F) != 0x61) return std::nullopt;
    Received r;
    r.sequence = (std::uint16_t) ((b[2] << 8) | b[3]);
    r.ssrc = get32 (b, 8);
    size_t at = 12;
    const auto header = b[at++];
    const bool longLength = (header & 0x80) != 0, firstDelta = (header & 0x20) != 0;
    size_t length = header & 0x0F;
    if (longLength)
    {
        if (at >= b.size()) return std::nullopt;
        length = (length << 8) | b[at++];
    }
    const size_t end = std::min (b.size(), at + length);
    std::uint8_t running = 0;
    bool first = true;
    while (at < end)
    {
        // A delta time before every command but the first (unless Z says so).
        if (! first || firstDelta)
            for (int i = 0; i < 4 && at < end; ++i)
                if ((b[at++] & 0x80) == 0) break;
        first = false;
        if (at >= end) break;
        std::uint8_t status = b[at];
        if (status >= 0x80) ++at;
        else if (running != 0) status = running;
        else return r;   // data with no status: the rest can't be read
        if (status >= 0xF8) { r.messages.push_back ({ status }); continue; }   // real time: no running status change
        Bytes m { status };
        const int n = dataBytesAfter (status);
        if (n < 0)
        {
            // System Exclusive: to F7 (a part ending F0 or F4 is kept as it is).
            while (at < end)
            {
                const auto x = b[at++];
                m.push_back (x);
                if (x == 0xF7 || x == 0xF0 || x == 0xF4) break;
            }
            running = 0;
        }
        else
        {
            for (int i = 0; i < n && at < end; ++i) m.push_back (b[at++]);
            if ((int) m.size() != n + 1) break;
            running = status < 0xF0 ? status : 0;
        }
        r.messages.push_back (std::move (m));
    }
    return r;
}

Bytes writeMidi (const std::vector<Bytes>& messages, std::uint16_t sequence, std::uint32_t timestamp, std::uint32_t ssrc)
{
    Bytes list;
    for (size_t i = 0; i < messages.size(); ++i)
    {
        if (i > 0) list.push_back (0);   // delta time: at once
        list.insert (list.end(), messages[i].begin(), messages[i].end());
    }
    Bytes b { 0x80, 0x61, (std::uint8_t) (sequence >> 8), (std::uint8_t) sequence };
    put32 (b, timestamp);
    put32 (b, ssrc);
    if (list.size() <= 15) b.push_back ((std::uint8_t) list.size());
    else
    {
        const auto n = std::min<size_t> (list.size(), 0x0FFF);
        b.push_back ((std::uint8_t) (0x80 | (n >> 8)));
        b.push_back ((std::uint8_t) n);
        list.resize (n);
    }
    b.insert (b.end(), list.begin(), list.end());
    return b;
}

} // namespace kal::rtpmidi
