/*
    Capture - retrospective recording (decision 0017).

    Kalibre is always listening. Every MIDI message from a keyboard is kept,
    with when it came (on a steady clock) and, if the song was playing, where
    in the song it came. Record > Capture MIDI turns the last performance -
    everything since the last pause of a few seconds - into a clip on the
    chosen track: where it was played if the song was playing, otherwise at
    the cursor, timed from the project's tempo.

    The audio half (the last minute of each armed or monitored track's input)
    is a ring buffer in the engine; this is the MIDI half, and the ring both
    halves use.

    The ring is single-producer, single-consumer and lock-free: the MIDI
    thread writes, the window reads. No JUCE in here.
*/

#pragma once

#include "Model.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace kal::capture
{

struct Event
{
    double wallSeconds = 0;     // the steady clock
    double songTicks = -1;      // where the playhead was, or -1 when stopped
    std::uint8_t bytes[3] {};
};

// Holds the last `Size` events, oldest overwritten first.
template <size_t Size>
class Ring
{
public:
    void push (const Event& e) noexcept
    {
        const auto w = written.load (std::memory_order_relaxed);
        events[w % Size] = e;
        written.store (w + 1, std::memory_order_release);
    }

    // The most recent events, oldest first.
    std::vector<Event> snapshot() const
    {
        const auto w = written.load (std::memory_order_acquire);
        const auto n = std::min<std::uint64_t> (w, Size);
        std::vector<Event> out;
        out.reserve (static_cast<size_t> (n));
        for (auto i = w - n; i < w; ++i) out.push_back (events[i % Size]);
        return out;
    }

    void clear() noexcept { written.store (0, std::memory_order_release); }

private:
    std::array<Event, Size> events {};
    std::atomic<std::uint64_t> written { 0 };
};

struct Result
{
    std::vector<Note> notes;    // ticks from `start`
    std::vector<MidiEvent> events;
    Tick start = 0;             // where the clip goes
    Tick length = 0;
    bool wasPlaying = false;
};

// The last performance in `events`: back from the newest to the first gap of
// at least `gapSeconds` with no key held. Notes still held at the end are
// ended at the last event. `cursor` places a performance played while stopped.
Result lastPerformance (const std::vector<Event>&, const TempoMap&, Tick cursor, double gapSeconds = 3.0);

} // namespace kal::capture
