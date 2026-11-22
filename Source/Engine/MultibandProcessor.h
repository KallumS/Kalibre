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
    own state.
*/

#pragma once

#include "Model.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <atomic>

namespace kal
{

class PluginManager;

PluginRef multibandRef();

class MultibandProcessor : public juce::AudioPluginInstance
{
public:
    static constexpr int maxBands = 4, perBand = 4;
    static constexpr float lowest = 30.0f, highest = 18000.0f;
    static constexpr float minRatio = 1.25f;   // neighbouring crossovers at least a third of an octave apart

    explicit MultibandProcessor (PluginManager&);
    ~MultibandProcessor() override;

    struct Slot
    {
        PluginRef ref;                                       // no format: an empty place
        std::unique_ptr<juce::AudioPluginInstance> plugin;   // null when its plug-in couldn't be loaded
        bool used() const { return ! ref.format.empty(); }
    };

    struct Band
    {
        std::array<Slot, perBand> slots;
        juce::AudioParameterBool* on = nullptr;
        juce::AudioParameterBool* solo = nullptr;
        juce::AudioParameterFloat* gainDb = nullptr;
        // The audio thread's.
        juce::AudioBuffer<float> buffer, held;
        int heldAt = 0, latency = 0;
        bool hasPlugins() const { for (auto& s : slots) if (s.plugin) return true; return false; }
        int used() const { int n = 0; for (auto& s : slots) n += s.used() ? 1 : 0; return n; }
    };

    juce::AudioParameterChoice* bandCount = nullptr;          // 2, 3 or 4
    std::array<juce::AudioParameterFloat*, maxBands - 1> crossover {};   // Hz, low to high
    int bands() const { return bandCount->getIndex() + 2; }
    const Band& band (int i) const { return bandsArray[(size_t) i]; }

    // Message thread.
    bool loadPlugin (int band, int slot, const PluginRef&, juce::String& error);
    void clearSlot (int band, int slot);
    void setBands (int count);
    // A crossover moved, kept between its neighbours (at least `minRatio` apart) and within the spectrum.
    void setCrossover (int index, float hz);
    float crossoverHz (int index) const;              // as played: in order, apart
    float bandLow (int b) const { return b == 0 ? 20.0f : crossoverHz (b - 1); }
    float bandHigh (int b) const { return b == bands() - 1 ? 20000.0f : crossoverHz (b); }
    static juce::String bandName (int b, int count);

    // A band's plug-in in a window of its own (as an X/Y Morph corner's, 0114).
    void showWindow (int band, int slot);
    void closeWindow (int band, int slot);
    bool windowOpen (int band, int slot) const { return windows[(size_t) (band * perBand + slot)] != nullptr; }
    juce::DocumentWindow* window (int band, int slot) { return windows[(size_t) (band * perBand + slot)].get(); }

    // The input's spectrum for the picture: magnitudes (dB) at `points` frequencies, 20 Hz to 20 kHz, evenly in octaves.
    static constexpr int points = 160;
    void spectrum (std::array<float, points>&);

    std::function<void()> onChanged;
    // Shown by the app: choose an audio effect for a band (set by the device rack).
    static std::function<void (std::function<void (const PluginRef&)>)> choosePlugin;

    //==========================================================================
    const juce::String getName() const override { return "Multiband"; }
    void fillInPluginDescription (juce::PluginDescription&) const override;
    void prepareToPlay (double, int) override;
    void releaseResources() override;
    void reset() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    double getTailLengthSeconds() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    PluginManager& plugins;
    std::array<Band, maxBands> bandsArray;
    struct Dsp;                     // the crossovers' filters and the picture's FFT (juce_dsp, kept out of this header)
    std::unique_ptr<Dsp> dspState;
    juce::CriticalSection lock;   // the audio thread only tries it
    juce::AudioBuffer<float> rest;
    double rate = 48000;
    int block = 512;
    std::array<std::unique_ptr<juce::DocumentWindow>, maxBands * perBand> windows;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    // The input, for the picture: a ring the audio thread writes and the window reads.
    static constexpr int fftOrder = 11, fftSize = 1 << fftOrder;
    std::array<float, fftSize> ring {};
    std::atomic<int> ringAt { 0 };
    std::array<float, points> shown {};

    void prepareSlot (Slot&);
    void updateLatency();
    void changed();
};

} // namespace kal
