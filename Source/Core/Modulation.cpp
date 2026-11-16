#include "Modulation.h"

#include <algorithm>

namespace kal::modulation
{

namespace
{
constexpr double twoPi = 6.283185307179586;
double wrap (double x) { return x - std::floor (x); }
} // namespace

double lfoShape (LfoShape shape, double phase, double held)
{
    phase = wrap (phase);
    switch (shape)
    {
        case LfoShape::Sine:          return std::sin (twoPi * phase);
        case LfoShape::Triangle:      return phase < 0.25 ? phase * 4.0 : (phase < 0.75 ? 2.0 - phase * 4.0 : phase * 4.0 - 4.0);
        case LfoShape::SawUp:         return phase * 2.0 - 1.0;
        case LfoShape::SawDown:       return 1.0 - phase * 2.0;
        case LfoShape::Square:        return phase < 0.5 ? 1.0 : -1.0;
        case LfoShape::SampleAndHold: return held;
    }
    return 0;
}

double tableAt (const std::vector<float>& table, double phase)
{
    if (table.empty()) return 0;
    const double x = wrap (phase) * static_cast<double> (table.size());
    const size_t i = static_cast<size_t> (x) % table.size();
    const size_t j = (i + 1) % table.size();
    const double f = x - std::floor (x);
    return table[i] + (table[j] - table[i]) * f;
}

std::vector<float> tableFromSamples (const float* samples, int frames, int size)
{
    std::vector<float> out (static_cast<size_t> (std::max (2, size)), 0.0f);
    if (! samples || frames <= 0) return out;
    // Each point is the average of its stretch of samples' level, signed by
    // their mean, so a drum loop becomes its rhythm and a pad its swell.
    float peak = 0;
    for (size_t i = 0; i < out.size(); ++i)
    {
        const auto a = static_cast<int> (static_cast<double> (i) / out.size() * frames);
        const auto b = std::max (a + 1, static_cast<int> (static_cast<double> (i + 1) / out.size() * frames));
        double sum = 0, abs = 0;
        for (int k = a; k < b && k < frames; ++k) { sum += samples[k]; abs += std::abs (samples[k]); }
        const double n = std::max (1, b - a);
        const double level = abs / n;
        out[i] = static_cast<float> (sum >= 0 ? level : -level);
        peak = std::max (peak, std::abs (out[i]));
    }
    if (peak > 0) for (auto& v : out) v /= peak;
    return out;
}

//==============================================================================
void Runner::prepare (double sr)
{
    sampleRate = sr > 0 ? sr : 48000;
    reset();
}

void Runner::reset()
{
    phase = 0;
    stage = Stage::Idle;
    env = 0;
    follow = 0;
    pendingOn = 0;
    held = 0;
    pendingOff = false;
    current = 0;
}

double Runner::nextRandom()
{
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    return static_cast<double> (random) / 4294967295.0 * 2.0 - 1.0;
}

double Runner::runEnvelope (const Modulator& m, double seconds, bool hasSustain)
{
    if (pendingOn > 0)
    {
        pendingOn = 0;
        stage = Stage::Attack;     // retriggered from where it is, so it never clicks
    }
    if (pendingOff)
    {
        pendingOff = false;
        if (hasSustain && stage != Stage::Idle) stage = Stage::Release;
    }
    const double sustain = hasSustain ? std::clamp (m.sustain, 0.0, 1.0) : 0.0;
    double left = seconds;
    int guard = 0;
    while (left > 0 && guard++ < 8)
    {
        switch (stage)
        {
            case Stage::Idle: env = 0; left = 0; break;
            case Stage::Attack:
            {
                const double rate = 1.0 / std::max (1e-4, m.attack);
                const double need = (1.0 - env) / rate;
                if (need > left) { env += rate * left; left = 0; }
                else { env = 1.0; left -= need; stage = Stage::Decay; }
                break;
            }
            case Stage::Decay:
            {
                const double rate = 1.0 / std::max (1e-4, m.decay);
                const double need = (env - sustain) / rate;
                if (need > left) { env -= rate * left; left = 0; }
                else { env = sustain; left -= need; stage = hasSustain ? Stage::Sustain : Stage::Idle; }
                break;
            }
            case Stage::Sustain: env = sustain; left = 0; break;
            case Stage::Release:
            {
                const double rate = 1.0 / std::max (1e-4, m.release);
                const double need = env / rate;
                if (need > left) { env -= rate * left; left = 0; }
                else { env = 0; left = 0; stage = Stage::Idle; }
                break;
            }
        }
    }
    return env;
}

double Runner::advance (const Modulator& m, const Clock& clock, double inputLevel)
{
    const double seconds = clock.samples / clock.sampleRate;
    switch (m.kind)
    {
        case ModKind::Lfo:
        case ModKind::Wavetable:
        {
            double ph;
            if (m.sync && clock.playing)
            {
                // Locked to the bar: the same place in the song gives the same value.
                ph = clock.ppq / std::max (1e-6, m.syncBeats) + m.phase;
            }
            else
            {
                const double rate = m.sync ? clock.bpm / 60.0 / std::max (1e-6, m.syncBeats) : m.rateHz;
                phase = wrap (phase + rate * seconds);
                ph = phase + m.phase;
            }
            const double cycle = std::floor (ph);
            if (cycle != lastCycle) { lastCycle = cycle; held_ = nextRandom(); }
            double v = m.kind == ModKind::Lfo ? lfoShape (m.shape, ph, held_) : tableAt (m.table, ph);
            if (! m.bipolar) v = v * 0.5 + 0.5;
            current = v;
            break;
        }
        case ModKind::Adsr: current = runEnvelope (m, seconds, true); break;
        case ModKind::Adr:  current = runEnvelope (m, seconds, false); break;
        case ModKind::EnvelopeFollower:
        {
            const double target = std::clamp (inputLevel * m.followGain, 0.0, 1.0);
            const double time = target > follow ? m.followAttack : m.followRelease;
            const double coeff = 1.0 - std::exp (-seconds / std::max (1e-4, time));
            follow += (target - follow) * coeff;
            current = follow;
            break;
        }
    }
    return current;
}

} // namespace kal::modulation
