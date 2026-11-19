/*
    Playback - a track's clips turned into what the audio thread plays.

    The audio thread can't walk the project (it may be changing) or allocate,
    so whenever the project changes the window compiles every track into a
    flat list of MIDI messages and audio regions in seconds, and swaps it in
    whole. The audio thread walks the list with an index.

    Compiling is where timing is applied:

      - Track delay (decision 0011): every event moves by the track's delay;
        a negative delay plays the track early, which is how a slow-attack
        sample library is pulled into time.
      - Articulations (decision 0013): a note's articulation sends its
        keyswitch, controller or program change just before it, can move
        it to another channel, and has its own delay on top of the track's.
      - Looped clips are unrolled, clip offsets and mutes respected, and
        notes are cut at the clip's end.
      - Audio clips that overlap crossfade over the overlap (decision 0030).

    Plug-in latency is not here: the audio graph lines plug-ins up with each
    other (plug-in delay compensation, decision 0016).

    No JUCE in here.
*/

#pragma once

#include "Model.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace kal::playback
{

struct MidiMessage
{
    double seconds = 0;
    std::uint8_t bytes[3] {};
    std::uint8_t size = 3;
    int order = 0;           // at the same moment: note-offs, then keyswitches and controllers, then note-ons
    // A note-on played differently each time (decision 0061): it plays
    // `chance` times in 100, its velocity moved up to `velocityRange` either
    // way, and up to `jitter` seconds after `seconds` (which is early by half
    // of it, so it can land either side of where it's written).
    std::uint8_t chance = 100;
    std::uint8_t velocityRange = 0;
    float jitter = 0;
};

// The audio thread's dice: no locks, no allocation.
class Dice
{
public:
    explicit Dice (std::uint32_t seed = 0x2545F491u) : state (seed ? seed : 1u) {}
    double next()   // 0 up to 1
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (double) (state >> 8) / 16777216.0;
    }

private:
    std::uint32_t state;
};

struct Rolled
{
    bool plays = true;
    std::uint8_t velocity = 100;
    double delay = 0;   // seconds after the message's time
};

// What one note-on does this time.
Rolled roll (const MidiMessage&, Dice&);

struct AudioRegion
{
    Id clip = 0;
    double startSeconds = 0;     // on the timeline, after the track delay
    double endSeconds = 0;
    double sourceSeconds = 0;    // where in the (warped) source the region starts
    double gain = 1.0;
    double fadeIn = 0, fadeOut = 0;   // seconds; equal-power curves (fadeGain)
    std::string file;
};

struct CompiledTrack
{
    Id track = 0;
    std::vector<MidiMessage> midi;      // sorted by seconds, then order
    std::vector<AudioRegion> audio;     // sorted by start
};

// The effective delay of a note, in seconds: the track's, its articulation's and its own.
double noteDelaySeconds (const Project&, const Track&, const Note&);

CompiledTrack compile (const Project&, const Track&);

// Every tracks' compiled lists, for the engine to swap in.
std::vector<CompiledTrack> compileAll (const Project&);

// An equal-power fade's gain, `into` seconds into a fade `length` long (0 to 1).
inline double fadeGain (double into, double length)
{
    if (length <= 0 || into >= length) return 1.0;
    if (into <= 0) return 0.0;
    return std::sin (1.5707963267948966 * into / length);
}

// The first message at or after `seconds` (binary search).
size_t firstAtOrAfter (const std::vector<MidiMessage>&, double seconds);

} // namespace kal::playback
