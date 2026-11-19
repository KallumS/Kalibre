#include "NoteSplit.h"

#include <algorithm>

namespace kal
{

void NoteSplit::setOutputs (int n) { count = std::clamp (n, 1, maxOutputs); }

void NoteSplit::push (std::vector<Message>& out, int output, int sample, std::uint8_t a, std::uint8_t b, std::uint8_t c, int size)
{
    if (out.size() >= out.capacity()) return;   // never grow on the audio thread
    Message m;
    m.output = output;
    m.sample = sample;
    m.bytes[0] = a;
    m.bytes[1] = b;
    m.bytes[2] = c;
    m.size = size;
    out.push_back (m);
}

void NoteSplit::flush (std::vector<Message>& out)
{
    if (pendingSize == 0) return;
    // A new chord, unless these come within the window of the one forming.
    if (pendingSeconds - chordStart > window || chordSize == 0)
    {
        for (int i = 0; i < chordSize; ++i) held[(size_t) chord[(size_t) i]].inChord = false;
        chordSize = 0;
        chordStart = pendingSeconds;
    }
    for (int i = 0; i < pendingSize && chordSize < (int) chord.size(); ++i)
    {
        chord[(size_t) chordSize++] = pending[(size_t) i];
        held[(size_t) pending[(size_t) i]].inChord = true;
    }
    // Ended before the chord was complete: out of it.
    int kept = 0;
    for (int i = 0; i < chordSize; ++i)
        if (held[(size_t) chord[(size_t) i]].inChord) chord[(size_t) kept++] = chord[(size_t) i];
    chordSize = kept;
    // Top to bottom (by pitch, then channel).
    std::sort (chord.begin(), chord.begin() + chordSize, [] (int a, int b) { return (a & 127) != (b & 127) ? (a & 127) > (b & 127) : a < b; });
    for (int i = 0; i < chordSize; ++i)
    {
        const int key = chord[(size_t) i];
        auto& h = held[(size_t) key];
        const int to = std::min (i, count - 1);
        const auto ch = (std::uint8_t) (key / 128), pitch = (std::uint8_t) (key & 127);
        if (h.output == to) continue;
        // Already started on another output: moved where it belongs.
        if (h.output >= 0) push (out, h.output, pendingSample, (std::uint8_t) (0x80 | ch), pitch, 0);
        h.output = to;
        push (out, to, pendingSample, (std::uint8_t) (0x90 | ch), pitch, h.velocity);
    }
    pendingSize = 0;
}

void NoteSplit::add (const std::uint8_t* b, int size, int sample, double seconds, std::vector<Message>& out)
{
    if (size < 1) return;
    const int status = b[0] & 0xF0, ch = b[0] & 15;
    if (pendingSize > 0 && (sample != pendingSample || seconds != pendingSeconds)) flush (out);
    if (status == 0x90 && size >= 3 && b[2] > 0)
    {
        const int key = ch * 128 + (b[1] & 127);
        auto& h = held[(size_t) key];
        if (h.output >= 0 || h.inChord) return;   // already sounding
        for (int i = 0; i < pendingSize; ++i) if (pending[(size_t) i] == key) return;
        h.velocity = b[2];
        if (pendingSize < (int) pending.size()) pending[(size_t) pendingSize++] = key;
        pendingSample = sample;
        pendingSeconds = seconds;
        return;
    }
    if ((status == 0x80 || status == 0x90) && size >= 3)
    {
        const int key = ch * 128 + (b[1] & 127);
        // Ending one not yet sent: it never starts.
        for (int i = 0; i < pendingSize; ++i)
            if (pending[(size_t) i] == key) { pending[(size_t) i] = pending[(size_t) --pendingSize]; return; }
        auto& h = held[(size_t) key];
        if (h.output >= 0) push (out, h.output, sample, (std::uint8_t) (0x80 | ch), b[1], b[2]);
        h.output = -1;
        h.inChord = false;
        return;
    }
    if (status == 0xA0 && size >= 3)
    {
        // A key's own pressure goes with its note.
        const auto& h = held[(size_t) (ch * 128 + (b[1] & 127))];
        if (h.output >= 0) push (out, h.output, sample, b[0], b[1], b[2]);
        return;
    }
    if (b[0] >= 0xF0) return;   // system messages stay
    for (int o = 0; o < count; ++o) push (out, o, sample, b[0], size > 1 ? b[1] : 0, size > 2 ? b[2] : 0, size);
}

void NoteSplit::allOff (int sample, std::vector<Message>& out)
{
    pendingSize = 0;
    chordSize = 0;
    for (int key = 0; key < (int) held.size(); ++key)
    {
        auto& h = held[(size_t) key];
        if (h.output >= 0) push (out, h.output, sample, (std::uint8_t) (0x80 | (key / 128)), (std::uint8_t) (key & 127), 0);
        h = {};
    }
}

} // namespace kal
