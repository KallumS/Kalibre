#include "TempoMap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace kal
{

TempoMap::TempoMap()
{
    tempoPoints.push_back ({ 0, 120.0, false });
    meterList.push_back ({ 0, 4, 4 });
}

//==============================================================================
void TempoMap::sortTempos()
{
    std::stable_sort (tempoPoints.begin(), tempoPoints.end(),
                      [] (const TempoPoint& a, const TempoPoint& b) { return a.tick < b.tick; });
    for (auto& p : tempoPoints) p.bpm = std::clamp (p.bpm, minBpm, maxBpm);
    if (tempoPoints.empty() || tempoPoints.front().tick != 0)
        tempoPoints.insert (tempoPoints.begin(), { 0, tempoPoints.empty() ? 120.0 : tempoPoints.front().bpm, false });
}

void TempoMap::setTempos (std::vector<TempoPoint> points)
{
    tempoPoints = std::move (points);
    for (auto& p : tempoPoints) p.tick = std::max<Tick> (0, p.tick);
    sortTempos();
    tempoPoints.erase (std::unique (tempoPoints.begin(), tempoPoints.end(),
                                    [] (const TempoPoint& a, const TempoPoint& b) { return a.tick == b.tick; }),
                       tempoPoints.end());
}

void TempoMap::setTempoAt (Tick tick, double bpm, bool ramp)
{
    tick = std::max<Tick> (0, tick);
    for (auto& p : tempoPoints)
        if (p.tick == tick) { p.bpm = std::clamp (bpm, minBpm, maxBpm); p.rampToNext = ramp; return; }
    tempoPoints.push_back ({ tick, bpm, ramp });
    sortTempos();
}

void TempoMap::removeTempoAt (Tick tick)
{
    if (tick == 0) return;
    tempoPoints.erase (std::remove_if (tempoPoints.begin(), tempoPoints.end(),
                                       [tick] (const TempoPoint& p) { return p.tick == tick; }),
                       tempoPoints.end());
}

double TempoMap::bpmAt (Tick tick) const
{
    size_t i = 0;
    while (i + 1 < tempoPoints.size() && tempoPoints[i + 1].tick <= tick) ++i;
    const auto& p = tempoPoints[i];
    if (! p.rampToNext || i + 1 >= tempoPoints.size()) return p.bpm;
    const auto& q = tempoPoints[i + 1];
    const double f = static_cast<double> (tick - p.tick) / static_cast<double> (q.tick - p.tick);
    return p.bpm + (q.bpm - p.bpm) * f;
}

//==============================================================================
void TempoMap::sortMeters()
{
    std::stable_sort (meterList.begin(), meterList.end(), [] (const Meter& a, const Meter& b) { return a.bar < b.bar; });
    if (meterList.empty() || meterList.front().bar != 0)
        meterList.insert (meterList.begin(), { 0, 4, 4 });
}

static bool validDenominator (int d) { return d == 1 || d == 2 || d == 4 || d == 8 || d == 16 || d == 32; }

void TempoMap::setMeters (std::vector<Meter> list)
{
    meterList.clear();
    for (auto m : list)
    {
        if (m.numerator < 1 || m.numerator > 64 || ! validDenominator (m.denominator)) continue;
        m.bar = std::max (0, m.bar);
        meterList.push_back (m);
    }
    sortMeters();
    meterList.erase (std::unique (meterList.begin(), meterList.end(),
                                  [] (const Meter& a, const Meter& b) { return a.bar == b.bar; }),
                     meterList.end());
}

void TempoMap::setMeterAt (int bar, int numerator, int denominator)
{
    if (numerator < 1 || numerator > 64 || ! validDenominator (denominator)) return;
    bar = std::max (0, bar);
    for (auto& m : meterList)
        if (m.bar == bar) { m.numerator = numerator; m.denominator = denominator; return; }
    meterList.push_back ({ bar, numerator, denominator });
    sortMeters();
}

void TempoMap::removeMeterAt (int bar)
{
    if (bar == 0) return;
    meterList.erase (std::remove_if (meterList.begin(), meterList.end(), [bar] (const Meter& m) { return m.bar == bar; }),
                     meterList.end());
}

const Meter& TempoMap::meterAtBar (int bar) const
{
    size_t i = 0;
    while (i + 1 < meterList.size() && meterList[i + 1].bar <= bar) ++i;
    return meterList[i];
}

Tick TempoMap::barStart (int bar) const
{
    Tick t = 0;
    for (size_t i = 0; i < meterList.size(); ++i)
    {
        const int from = meterList[i].bar;
        const int to = i + 1 < meterList.size() ? meterList[i + 1].bar : bar;
        if (bar <= from) break;
        t += static_cast<Tick> (std::min (bar, to) - from) * meterList[i].barTicks();
        if (bar <= to) break;
    }
    if (bar < 0) t = static_cast<Tick> (bar) * meterList.front().barTicks();
    return t;
}

int TempoMap::barAt (Tick tick) const
{
    if (tick < 0) return static_cast<int> ((tick - meterList.front().barTicks() + 1) / meterList.front().barTicks());
    Tick start = 0;
    for (size_t i = 0; i < meterList.size(); ++i)
    {
        const auto& m = meterList[i];
        if (i + 1 < meterList.size())
        {
            const Tick next = start + static_cast<Tick> (meterList[i + 1].bar - m.bar) * m.barTicks();
            if (tick < next) return m.bar + static_cast<int> ((tick - start) / m.barTicks());
            start = next;
        }
        else
            return m.bar + static_cast<int> ((tick - start) / m.barTicks());
    }
    return 0;
}

const Meter& TempoMap::meterAt (Tick tick) const { return meterAtBar (barAt (tick)); }

BarBeat TempoMap::barBeatAt (Tick tick) const
{
    BarBeat bb;
    bb.bar = barAt (tick);
    const auto& m = meterAtBar (bb.bar);
    const Tick into = tick - barStart (bb.bar);
    bb.beat = static_cast<int> (into / m.beatTicks());
    bb.tick = into % m.beatTicks();
    return bb;
}

Tick TempoMap::beatStartAtOrBefore (Tick tick) const
{
    const auto bb = barBeatAt (tick);
    return tick - bb.tick;
}

//==============================================================================
// Seconds from `from` to `to`, both inside tempo segment `index`.
double TempoMap::secondsOfSegment (size_t index, Tick from, Tick to) const
{
    const auto& p = tempoPoints[index];
    const double beats = static_cast<double> (to - from) / PPQ;
    if (! p.rampToNext || index + 1 >= tempoPoints.size())
        return beats * 60.0 / p.bpm;

    // A linear ramp in BPM: integrate 60 / bpm(t) dt over ticks.
    const auto& q = tempoPoints[index + 1];
    const double span = static_cast<double> (q.tick - p.tick);
    const double slope = (q.bpm - p.bpm) / span;          // BPM per tick
    const double b0 = p.bpm + slope * static_cast<double> (from - p.tick);
    const double b1 = p.bpm + slope * static_cast<double> (to - p.tick);
    if (std::abs (slope) < 1e-12) return beats * 60.0 / b0;
    return 60.0 / PPQ * std::log (b1 / b0) / slope;
}

double TempoMap::secondsAt (Tick tick) const
{
    if (tick <= 0) return static_cast<double> (tick) / PPQ * 60.0 / tempoPoints.front().bpm;
    double s = 0;
    for (size_t i = 0; i < tempoPoints.size(); ++i)
    {
        const Tick a = tempoPoints[i].tick;
        const Tick b = i + 1 < tempoPoints.size() ? tempoPoints[i + 1].tick : tick;
        if (tick <= a) break;
        s += secondsOfSegment (i, a, std::min (tick, b));
        if (tick <= b) break;
    }
    return s;
}

double TempoMap::ticksAt (double seconds) const
{
    if (seconds <= 0) return seconds * tempoPoints.front().bpm / 60.0 * PPQ;
    double s = 0;
    for (size_t i = 0; i < tempoPoints.size(); ++i)
    {
        const auto& p = tempoPoints[i];
        const bool last = i + 1 >= tempoPoints.size();
        const double segSeconds = last ? 1e300 : secondsOfSegment (i, p.tick, tempoPoints[i + 1].tick);
        if (seconds <= s + segSeconds)
        {
            const double into = seconds - s;
            if (! p.rampToNext || last)
                return static_cast<double> (p.tick) + into * p.bpm / 60.0 * PPQ;
            const auto& q = tempoPoints[i + 1];
            const double slope = (q.bpm - p.bpm) / static_cast<double> (q.tick - p.tick);
            if (std::abs (slope) < 1e-12)
                return static_cast<double> (p.tick) + into * p.bpm / 60.0 * PPQ;
            // Invert seconds = 60/PPQ * ln(b/p.bpm)/slope.
            const double b = p.bpm * std::exp (into * slope * PPQ / 60.0);
            return static_cast<double> (p.tick) + (b - p.bpm) / slope;
        }
        s += segSeconds;
    }
    return 0;
}

Tick TempoMap::tickAt (double seconds) const { return static_cast<Tick> (std::llround (ticksAt (seconds))); }

std::int64_t TempoMap::sampleAt (Tick tick, double sampleRate) const
{
    return static_cast<std::int64_t> (std::llround (secondsAt (tick) * sampleRate));
}

//==============================================================================
std::string TempoMap::formatBarBeat (Tick tick) const
{
    const auto bb = barBeatAt (tick);
    char buf[48];
    std::snprintf (buf, sizeof buf, "%d.%d.%03d", bb.bar + 1, bb.beat + 1, static_cast<int> (bb.tick));
    return buf;
}

std::string TempoMap::formatMinutes (double seconds)
{
    const bool negative = seconds < 0;
    seconds = std::abs (seconds);
    const int m = static_cast<int> (seconds / 60.0);
    const double s = seconds - m * 60.0;
    char buf[48];
    std::snprintf (buf, sizeof buf, "%s%d:%06.3f", negative ? "-" : "", m, s);
    return buf;
}

std::string TempoMap::formatTimecode (double seconds, double fps)
{
    seconds = std::max (0.0, seconds);
    const auto totalFrames = static_cast<long long> (std::floor (seconds * fps + 1e-9));
    const long long f = totalFrames % static_cast<long long> (fps);
    const long long totalSeconds = totalFrames / static_cast<long long> (fps);
    char buf[48];
    std::snprintf (buf, sizeof buf, "%02lld:%02lld:%02lld:%02lld",
                   totalSeconds / 3600, (totalSeconds / 60) % 60, totalSeconds % 60, f);
    return buf;
}

} // namespace kal
