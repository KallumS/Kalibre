/*
    NoteSplit - the Note Splitter's rule (decision 0083): a chord's notes,
    top to bottom, each to its own output: the top note to output 1, the next
    down to output 2, and so on; any below the last output go to the last.
    Each output is another track's MIDI input.

    Notes starting at the same moment are one chord, and so are notes played
    within a short window of the chord's first (a hand never lands all at
    once): a note that joins a chord may move one already started to its
    rightful output, ended on the old and started again on the new. A note
    held from before stays where it is. A note's end goes where its start
    went. Everything else (controllers, the sustain pedal, pitch bend,
    pressure) goes to every output; a key's own pressure follows its note.

    No JUCE, and nothing allocated while playing.
*/

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace kal
{

class NoteSplit
{
public:
    static constexpr int maxOutputs = 16;

    struct Message
    {
        int output = 0;            // 0-based
        int sample = 0;            // within the block
        std::uint8_t bytes[3] {};
        int size = 3;
    };

    void setOutputs (int n);
    int outputs() const { return count; }
    double window = 0.03;          // seconds: notes this close together are one chord

    // One message, in time order. `seconds` is when it is, on any clock that
    // keeps going; messages of one moment come together, so whatever order a
    // chord's notes arrive in, the top goes to output 1 (call flush() after
    // the last message of a block, or of a moment, to send the chord's notes).
    void add (const std::uint8_t* bytes, int size, int sample, double seconds, std::vector<Message>& out);
    void flush (std::vector<Message>& out);

    // Every note sounding, ended (a stop, a jump).
    void allOff (int sample, std::vector<Message>& out);

private:
    int count = 4;
    struct Held { int output = -1; std::uint8_t velocity = 0; bool inChord = false; };
    std::array<Held, 16 * 128> held {};
    // The chord forming: its notes (channel * 128 + pitch), when it began.
    std::array<int, 128> chord {};
    int chordSize = 0;
    double chordStart = -1.0e9;
    // Note-ons of the moment not yet sent, and when.
    std::array<int, 128> pending {};
    int pendingSize = 0, pendingSample = 0;
    double pendingSeconds = -1.0e9;

    static void push (std::vector<Message>& out, int output, int sample, std::uint8_t a, std::uint8_t b, std::uint8_t c, int size = 3);
};

} // namespace kal
