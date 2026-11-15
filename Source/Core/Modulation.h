/*
    Modulation - the modulators' running state (decision 0012).

    A modulator makes a value that moves - an LFO, an ADSR or ADR envelope
    started by the track's notes, an envelope follower riding a track's
    level, or a wavetable: any one-cycle shape, drawn or read from a sample,
    played like an LFO. Each has targets: device parameters it pushes up and
    down by an amount, on top of where the parameter (or its automation) sits.

    The engine runs one of these per modulator, once a block, on the audio
    thread: nothing here allocates once prepared.

    No JUCE in here.
*/

#pragma once

#include "Model.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace kal::modulation
{

// One cycle of an LFO shape at phase 0-1, from -1 to 1. Sample-and-hold
// takes the value it is given (a fresh random number each cycle).
double lfoShape (LfoShape, double phase, double held = 0.0);

// A value read from a one-cycle table at phase 0-1, interpolated.
double tableAt (const std::vector<float>&, double phase);

// A table read from audio: `frames` samples squeezed (or stretched) to `size`
// points, normalised to -1..1. This is how a sample becomes a modulation curve.
std::vector<float> tableFromSamples (const float* samples, int frames, int size = 512);

struct Clock
{
    double sampleRate = 48000;
    int samples = 512;        // in this block
    bool playing = false;
    double ppq = 0;           // the playhead in quarter notes, at the block's start
    double bpm = 120;
};

class Runner
{
public:
    void prepare (double sampleRate);
    void reset();

    // Notes that started and ended in this block (counts), and the level
    // the envelope follower hears (peak, linear).
    void noteOn()  { ++pendingOn; ++held; }
    void noteOff() { if (held > 0) --held; if (held == 0) pendingOff = true; }

    // The modulator's value for this block: 0-1 for envelopes and followers,
    // -1..1 for bipolar LFOs and tables (0-1 when not bipolar).
    double advance (const Modulator&, const Clock&, double inputLevel);

    double value() const { return current; }

private:
    double sampleRate = 48000;
    double phase = 0;
    double held_ = 0;
    double lastCycle = -1e300;
    std::uint32_t random = 0x9e3779b9u;
    double current = 0;

    // Envelopes
    enum class Stage { Idle, Attack, Decay, Sustain, Release };
    Stage stage = Stage::Idle;
    double env = 0;
    int pendingOn = 0;
    int held = 0;
    bool pendingOff = false;

    // Follower
    double follow = 0;

    double nextRandom();
    double runEnvelope (const Modulator&, double seconds, bool sustainStage);
};

// A parameter's value with its modulators applied, clamped to 0-1.
inline double applied (double base, double modulatorValue, double amount)
{
    const double v = base + modulatorValue * amount;
    return v < 0 ? 0 : (v > 1 ? 1 : v);
}

} // namespace kal::modulation
