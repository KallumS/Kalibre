/*
    StereoSplitProcessor - Kalibre's Stereo Split device (decision 0127).

    The sound split by where it sits between the speakers, with up to four
    plug-ins on each part, each part with On, Solo and its own volume (the
    parts are SplitProcessor's, shared with the Multiband, 0126):

    - Left, Middle, Right: each frequency, moment by moment (a 2048-point
      short-time Fourier transform, a quarter overlapping), goes to the part
      its place between the speakers falls in, judged by how loud it is on
      each side; two edges, dragged on the vectorscope, never past one
      another, softened over a tenth of the way so nothing clicks across
      them. The parts add back up to the sound, 2048 samples (the window)
      later: the device's delay in this mode, whether anything's in it or not,
      so switching a plug-in in or out never moves the sound.
    - Middle and Sides: what both sides share, (L+R)/2 in both, and what
      differs, (L-R)/2 on the left and its opposite on the right; no delay.
      Middle's plug-ins are the same in both modes.

    With nothing to do the input passes untouched (held back by the window in
    Left, Middle, Right), proved by a cancellation test.

    The picture: a vectorscope (the sound's samples, louder further out, the
    middle straight up, left and right on the diagonals) and each frequency's
    place on it, coloured from low to high, the parts shaded behind.
*/

#pragma once

#include "SplitProcessor.h"

namespace kal
{

PluginRef stereoSplitRef();

class StereoSplitProcessor : public SplitProcessor,
                             private juce::AudioProcessorParameter::Listener,
                             private juce::AsyncUpdater
{
public:
    enum PartIndex { left = 0, middle = 1, right = 2, sides = 3 };
    enum class Mode { leftMiddleRight = 0, middleSides = 1 };
    static constexpr float softness = 0.1f;   // an edge's soft width, in pan (-1 left to 1 right)
    static constexpr int fftOrder = 11, fftSize = 1 << fftOrder, hop = fftSize / 4;

    explicit StereoSplitProcessor (PluginManager&);
    ~StereoSplitProcessor() override;

    juce::AudioParameterChoice* modeParam = nullptr;
    std::array<juce::AudioParameterFloat*, 2> edgeParam {};   // pan, left edge then right

    Mode mode() const { return (Mode) modeInPlay.load(); }
    void setMode (Mode);
    // An edge as played: in order, at least `softness` apart, -1 to 1.
    float edge (int k) const;
    void setEdge (int k, float pan);
    static juce::String panText (float pan);

    InUse partsInUse() const override;
    juce::String partName (int) const override;
    juce::String partDetail (int) const override;

    // The picture, read by its window.
    static constexpr int points = 96, scopeSamples = 1024;
    struct Picture
    {
        std::array<float, points> pan {}, level {};   // each frequency's place (-1 to 1) and level (dB), 30 Hz to 16 kHz in octaves
        std::array<float, scopeSamples * 2> samples {};   // the last samples, left and right in turn
    };
    void picture (Picture&);
    static float pointHz (int i);

    juce::AudioProcessorEditor* createEditor() override;

protected:
    void split (const juce::AudioBuffer<float>&, int n) override;
    int splitLatency() const override { return mode() == Mode::leftMiddleRight ? fftSize : 0; }
    void prepareSplit (double, int) override;
    void resetSplit() override;
    void listen (const juce::AudioBuffer<float>&, int) override;
    void stateLoaded() override;

private:
    std::atomic<int> modeInPlay { 0 };
    struct Stft;   // juce_dsp, kept out of this header
    std::unique_ptr<Stft> stft;
    void frame();

    // The input, for the picture: a ring the audio thread writes and the window reads.
    std::array<float, fftSize> ringL {}, ringR {};
    std::atomic<int> ringAt { 0 };
    std::array<float, points> shownLevel {};

    void parameterValueChanged (int, float) override;
    void parameterGestureChanged (int, bool) override {}
    void handleAsyncUpdate() override;
    void modeChanged();
};

} // namespace kal
