/*
    WorkClock - how long the user has been making music in a song (decision
    0091), the top bar's timer. The clock is DAW Battle Pass's (KallumS/
    DAW-Battle-Pass, `G.tick` in reascripts/bp_game.lua, its decisions 0002
    and 0003): active time, not wall time.

    - The clock runs while something is happening: an edit, the mouse moving
      or a key pressed in Kalibre's window, a note played in, the transport
      starting or stopping. With nothing for 3 minutes, the user is away and
      it stops; it starts again with the next thing.
    - While the song plays back the user is listening: at least 8 minutes
      before it stops. Recording always counts.
    - Each step counts at most a second, so a stall (the Mac asleep, a long
      render) is never counted as work.
    - Up to the idle time is counted after the user walks away: taking it back
      would make the timer run backwards, which is worse than three generous
      minutes (the battle pass's reasoning).

    The time is always passed in (`tp`, seconds on any steady clock), so the
    tests can run hours in a moment. No JUCE in here.
*/

#pragma once

#include <limits>
#include <string>

namespace kal
{

struct WorkClock
{
    static constexpr double idleSeconds = 180;     // nothing for this long: away
    static constexpr double idlePlaying = 480;     // ...unless the song is playing back
    static constexpr double longestStep = 1.0;     // a step never counts more than this

    double active = 0;                             // seconds of music-making in the song
    bool idle = false;

    // A song opened or started: its time so far, the clock running from now.
    void begin (double seconds, double tp);
    // Called often (several times a second). `input`: something happened since the last call.
    void tick (double tp, bool input, bool playing, bool recording);

    // "0:42", "12:05", "1:02:09": hours, minutes, seconds, as a stopwatch shows them.
    static std::string format (double seconds);

private:
    double now = std::numeric_limits<double>::quiet_NaN();
    double lastInput = -std::numeric_limits<double>::infinity();
};

} // namespace kal
