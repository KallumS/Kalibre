/*
    LoudnessMatch - keeps what a plug-in sends out as loud as what it was
    sent (decision 0056), by short-term loudness: LUFS-S, K-weighted, three
    seconds (BS.1770, Core/Loudness). The gain glides over about a second,
    goes no further than 24 dB either way, and only moves while both sides
    are above -70 LUFS, so silence and fades don't pull it about.

    Audio thread: keepInput before the plug-in runs, apply after. Nothing is
    allocated there; prepare sizes everything first.
*/

#pragma once

#include "Loudness.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cmath>

namespace kal
{

class LoudnessMatch
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        rate = sampleRate;
        in.prepare (sampleRate);
        out.prepare (sampleRate);
        input.setSize (2, juce::jmax (1, maxBlock));
        db = 0;
        appliedDb = 0;
    }

    void keepInput (const juce::AudioBuffer<float>& b, int n)
    {
        kept = n <= input.getNumSamples() && b.getNumChannels() > 0;
        if (! kept) return;
        for (int ch = 0; ch < 2; ++ch) input.copyFrom (ch, 0, b, juce::jmin (ch, b.getNumChannels() - 1), 0, n);
    }

    void apply (juce::AudioBuffer<float>& b, int n, bool on)
    {
        if (! kept || b.getNumChannels() == 0) return;
        const int right = juce::jmin (1, b.getNumChannels() - 1);
        in.process (input.getReadPointer (0), input.getReadPointer (1), n);
        out.process (b.getReadPointer (0), b.getReadPointer (right), n);
        float target = db;
        if (! on) target = 0;
        else if (in.shortTerm() > -70 && out.shortTerm() > -70) target = (float) juce::jlimit (-24.0, 24.0, in.shortTerm() - out.shortTerm());
        const float before = db;
        db += (target - db) * (1.0f - std::exp (-(float) n / (float) rate));
        if (! on && std::abs (db) < 0.001f) db = 0;   // fully back: nothing more to do
        appliedDb = db;
        if (std::abs (before) > 0.001f || std::abs (db) > 0.001f)
            for (int ch = 0; ch < juce::jmin (2, b.getNumChannels()); ++ch)
                b.applyGainRamp (ch, 0, n, juce::Decibels::decibelsToGain (before), juce::Decibels::decibelsToGain (db));
    }

    std::atomic<float> appliedDb { 0 };

private:
    loudness::Meter in, out;
    juce::AudioBuffer<float> input;
    double rate = 48000;
    float db = 0;
    bool kept = false;
};

} // namespace kal
