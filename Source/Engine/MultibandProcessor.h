/*
    MultibandProcessor - Kalibre's Multiband device (decision 0126).

    The spectrum cut into 2, 3 or 4 bands at crossovers dragged on a picture
    of it, never past one another, with up to four plug-ins in each band, one
    after another, each band with On, Solo and its own volume.

    Split as multiband processors split (Linkwitz-Riley, 24 dB an octave): at
    each crossover in turn, below and above; each band below turned by an
    all-pass at the crossovers above its own, so the bands add back up to the
    input's level at every frequency (only the phase turns, near the
    crossovers), each band cleanly its own. With no plug-in anywhere and every
    band as it was, the sound isn't split at all and passes untouched (proved
    by a cancellation test). A band's plug-ins'
    delay holds the other bands back to meet it; the device's delay is the
    longest band's.

    Its plug-ins' windows open in windows of their own (as an X/Y Morph
    corner's, 0114). Every setting is a parameter; the plug-ins are kept in its
    own state. The parts, their plug-ins and windows are SplitProcessor's,
    shared with Stereo Split (0127).
*/

#pragma once

#include "SplitProcessor.h"

#include <array>
#include <atomic>

namespace kal
{

PluginRef multibandRef();

class MultibandProcessor : public SplitProcessor
{
public:
    static constexpr int maxBands = 4, perBand = perPart;
    static constexpr float lowest = 30.0f, highest = 18000.0f;
    static constexpr float minRatio = 1.25f;   // neighbouring crossovers at least a third of an octave apart

    explicit MultibandProcessor (PluginManager&);
    ~MultibandProcessor() override;

    using Band = Part;
    juce::AudioParameterChoice* bandCount = nullptr;          // 2, 3 or 4
    std::array<juce::AudioParameterFloat*, maxBands - 1> crossover {};   // Hz, low to high
    int bands() const { return bandCount->getIndex() + 2; }
    const Band& band (int i) const { return part (i); }

    InUse partsInUse() const override;
    juce::String partName (int b) const override { return bandName (b, bands()); }
    juce::String partDetail (int b) const override;

    // Message thread.
    void setBands (int count);
    // A crossover moved, kept between its neighbours (at least `minRatio` apart) and within the spectrum.
    void setCrossover (int index, float hz);
    float crossoverHz (int index) const;              // as played: in order, apart
    float bandLow (int b) const { return b == 0 ? 20.0f : crossoverHz (b - 1); }
    float bandHigh (int b) const { return b == bands() - 1 ? 20000.0f : crossoverHz (b); }
    static juce::String bandName (int b, int count);

    // The input's spectrum for the picture: magnitudes (dB) at `points` frequencies, 20 Hz to 20 kHz, evenly in octaves.
    static constexpr int points = 160;
    void spectrum (std::array<float, points>&);

    juce::AudioProcessorEditor* createEditor() override;

protected:
    void split (const juce::AudioBuffer<float>&, int n) override;
    void prepareSplit (double, int) override;
    void resetSplit() override;
    void listen (const juce::AudioBuffer<float>&, int) override;

private:
    struct Dsp;                     // the crossovers' filters and the picture's FFT (juce_dsp, kept out of this header)
    std::unique_ptr<Dsp> dspState;
    juce::AudioBuffer<float> rest;

    // The input, for the picture: a ring the audio thread writes and the window reads.
    static constexpr int fftOrder = 11, fftSize = 1 << fftOrder;
    std::array<float, fftSize> ring {};
    std::atomic<int> ringAt { 0 };
    std::array<float, points> shown {};
};

} // namespace kal
