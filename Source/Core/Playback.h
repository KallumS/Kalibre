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

    Plug-in latency is not here: the audio graph lines plug-ins up with each
    other (plug-in delay compensation, decision 0016).

    No JUCE in here.
*/

#pragma once

#include "Model.h"

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
};

struct AudioRegion
{
    Id clip = 0;
    double startSeconds = 0;     // on the timeline, after the track delay
    double endSeconds = 0;
    double sourceSeconds = 0;    // where in the (warped) source the region starts
    double gain = 1.0;
    double fadeIn = 0, fadeOut = 0;
    std::string file;
};

struct CompiledTrack
{
    Id track = 0;
    std::vector<MidiMessage> midi;      // sorted by seconds, then order
    std::vector<AudioRegion> audio;     // sorted by start
};

// The effective delay of a note, in seconds: the track's plus its articulation's.
double noteDelaySeconds (const Project&, const Track&, const Note&);

CompiledTrack compile (const Project&, const Track&);

// Every tracks' compiled lists, for the engine to swap in.
std::vector<CompiledTrack> compileAll (const Project&);

// The first message at or after `seconds` (binary search).
size_t firstAtOrAfter (const std::vector<MidiMessage>&, double seconds);

} // namespace kal::playback
