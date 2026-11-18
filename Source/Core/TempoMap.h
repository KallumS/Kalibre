/*
    TempoMap - where every beat falls in seconds.

    Positions in Kalibre are ticks, 960 to a quarter note, so a note stays on
    its beat when the tempo changes. The tempo map turns ticks into seconds and
    back, and bars into ticks.

    Tempo is automatable (decision 0004): it is a list of points, each either
    held until the next point (a step) or ramped linearly to it, so a piece can
    start at 90 BPM, speed up to 120 and slow down again. Time signatures are
    automatable too, as changes that take effect at the start of a bar - a
    meter can only change on a bar line.

    No JUCE in here.
*/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kal
{

using Tick = std::int64_t;
inline constexpr Tick PPQ = 960;

struct TempoPoint
{
    Tick tick = 0;
    double bpm = 120.0;
    bool rampToNext = false;   // linear in BPM from here to the next point

    bool operator== (const TempoPoint&) const = default;
};

struct Meter
{
    int bar = 0;           // the bar it starts on, counted from 0
    int numerator = 4;
    int denominator = 4;   // 1, 2, 4, 8, 16, 32

    Tick barTicks() const  { return beatTicks() * numerator; }
    Tick beatTicks() const { return PPQ * 4 / denominator; }

    bool operator== (const Meter&) const = default;
};

struct BarBeat
{
    int bar = 0;        // from 0; shown from 1
    int beat = 0;       // from 0; shown from 1
    Tick tick = 0;      // into the beat
};

class TempoMap
{
public:
    TempoMap();

    // Tempo ------------------------------------------------------------------
    const std::vector<TempoPoint>& tempos() const { return tempoPoints; }
    void setTempos (std::vector<TempoPoint>);
    void setTempoAt (Tick, double bpm, bool ramp = false);   // adds or replaces the point at that tick
    void removeTempoAt (Tick);                                // the first point can't go
    double bpmAt (Tick) const;

    static constexpr double minBpm = 10.0;
    static constexpr double maxBpm = 999.0;

    // Meter ------------------------------------------------------------------
    const std::vector<Meter>& meters() const { return meterList; }
    void setMeters (std::vector<Meter>);
    void setMeterAt (int bar, int numerator, int denominator);
    void removeMeterAt (int bar);
    const Meter& meterAtBar (int bar) const;
    const Meter& meterAt (Tick) const;

    Tick barStart (int bar) const;
    int barAt (Tick) const;
    BarBeat barBeatAt (Tick) const;
    Tick beatStartAtOrBefore (Tick) const;

    // Time -------------------------------------------------------------------
    double secondsAt (Tick) const;
    Tick tickAt (double seconds) const;
    double ticksAt (double seconds) const;   // unrounded, for the audio thread's clock

    std::int64_t sampleAt (Tick, double sampleRate) const;

    // "5.2.240" (bar.beat.tick, from 1)
    std::string formatBarBeat (Tick) const;
    // A length in words, as a musician says it: "1/8", "3/16", "1 beat",
    // "4 bars", "2 bars 1 beat" (bars and beats by the meter at `start`).
    std::string describeLength (Tick start, Tick length) const;
    // "1:02.345"
    static std::string formatMinutes (double seconds);
    // "00:01:02:08" at a frame rate
    static std::string formatTimecode (double seconds, double fps = 25.0);

    bool operator== (const TempoMap&) const = default;

private:
    std::vector<TempoPoint> tempoPoints;
    std::vector<Meter> meterList;

    double secondsOfSegment (size_t index, Tick from, Tick to) const;
    void sortTempos();
    void sortMeters();
};

} // namespace kal
