/*
    TimedPlugin - a plug-in in the graph, with a stopwatch round it (decision 0038).

    The graph holds this, not the plug-in itself, so each plug-in's time in
    the audio thread can be added up per track: the mixer's CPU readings.
    Everything else passes straight through: its buses (copied from the
    plug-in, sidechain included, so channel numbers are the same), latency,
    tail, MIDI, bypass, the play head, offline rendering and extensions
    (ARA, VST3). Engine::instance() hands out the plug-in inside, so editors,
    parameters and state never see the wrapper.
*/

#pragma once

#include "CrashContext.h"
#include "LoudnessMatch.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace kal
{

// Where the MIDI going into a plug-in, and coming out of it, is caught while
// a track is armed for MIDI (decision 0064): a track's Source is one. Called
// on the audio thread, once with the plug-in's input and once with its output.
struct MidiTap
{
    virtual ~MidiTap() = default;
    virtual void caught (const juce::MidiBuffer&, bool fromOutput) = 0;
};

class TimedPlugin : public juce::AudioPluginInstance
{
public:
    explicit TimedPlugin (std::unique_ptr<juce::AudioPluginInstance> p)
        : AudioPluginInstance (busesOf (*p)), inner (std::move (p))
    {
        crashName = inner->getName().toStdString();
        setLatencySamples (inner->getLatencySamples());
    }

    juce::AudioPluginInstance& plugin() { return *inner; }

    // Audio-thread time spent in the plug-in, in high-resolution ticks.
    std::atomic<juce::int64> busyTicks { 0 };

    // A plug-in of a version not playing (decision 0050): loaded, kept, silent,
    // and costing nothing, so choosing its version is instant.
    std::atomic<bool> parked { false };

    // The track's tap, on the device whose MIDI is recorded when armed for MIDI (decision 0064).
    std::atomic<MidiTap*> tap { nullptr };

    // Match Loudness (decision 0056): what comes out kept as loud as what went in.
    std::atomic<bool> matchLoudness { false };
    LoudnessMatch match;

    // Its parameters' automation for this block (decision 0089), drawn by the track's
    // Source, which plays before it: each value at every sample, and where a lane's
    // point falls. The block is played in pieces, cut at every point and every 16
    // samples along a ramp, each piece with the values at its start: plug-ins take
    // a parameter's value once a call, so this is how a change lands on its sample.
    struct AutoCurve { juce::AudioProcessorParameter* param = nullptr; std::vector<float> values; };
    std::array<AutoCurve, 16> curves;
    std::vector<std::uint8_t> breaks;
    int curveCount = 0;
    const void* curveOwner = nullptr;
    std::uint32_t curveSerial = 0;
    std::atomic<int> curveSamples { 0 };
    int curveCapacity() const { return (int) breaks.size(); }
    static constexpr int rampPiece = 16;

    // Kept in step when the plug-in's latency changes (the engine is told), message thread.
    // How late the sound reaching it is after the song position its track's
    // automation is drawn for: the plug-ins before it (0096). Set by the engine.
    std::atomic<int> automationDelay { 0 };

    void latencyChanged()
    {
        setLatencySamples (inner->getLatencySamples());
        makeBypassDelay();
    }

    //==========================================================================
    const juce::String getName() const override { return inner->getName(); }
    void fillInPluginDescription (juce::PluginDescription& d) const override { inner->fillInPluginDescription (d); }
    juce::AudioPluginExtensions::ARAClient* getARAClient() override { return inner->getARAClient(); }
    const juce::AudioPluginExtensions::ARAClient* getARAClient() const override { return inner->getARAClient(); }
    juce::AudioPluginExtensions::AudioUnitClient* getAudioUnitClient() override { return inner->getAudioUnitClient(); }
    const juce::AudioPluginExtensions::AudioUnitClient* getAudioUnitClient() const override { return inner->getAudioUnitClient(); }
    juce::AudioPluginExtensions::VSTClient* getVSTClient() override { return inner->getVSTClient(); }
    const juce::AudioPluginExtensions::VSTClient* getVSTClient() const override { return inner->getVSTClient(); }
    juce::AudioPluginExtensions::VST3Client* getVST3Client() override { return inner->getVST3Client(); }
    const juce::AudioPluginExtensions::VST3Client* getVST3Client() const override { return inner->getVST3Client(); }

    void prepareToPlay (double rate, int block) override
    {
        inner->setRateAndBufferSizeDetails (rate, block);
        inner->prepareToPlay (rate, block);
        setLatencySamples (inner->getLatencySamples());
        makeBypassDelay();
        match.prepare (rate, block);
        const int most = juce::jmax (block, 8192);
        for (auto& c : curves) c.values.assign ((size_t) most, 0.0f);
        breaks.assign ((size_t) most, 0);
        curveSamples = 0;
        pieceMidi.ensureSize (8192);
        joinedMidi.ensureSize (8192);
    }
    void releaseResources() override { inner->releaseResources(); }
    // Everything it holds from what it last played let go: an export starts clean (0097).
    void reset() override
    {
        inner->reset();
        if (auto* ring = bypassRing.load()) ring->clear();
        bypassAt = 0;
    }

    void processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override
    {
        if (parked.load (std::memory_order_relaxed)) { audio.clear(); midi.clear(); return; }
        const auto start = juce::Time::getHighResolutionTicks();
        const bool matching = matchLoudness.load (std::memory_order_relaxed);
        if (matching) match.keepInput (audio, audio.getNumSamples());
        auto* t = tap.load (std::memory_order_relaxed);
        if (t != nullptr) t->caught (midi, false);
        {
            const juce::ScopedLock lock (inner->getCallbackLock());
            if (inner->isSuspended()) { audio.clear(); midi.clear(); }
            else
            {
                // Named for the crash report while it runs (decision 0073).
                crashcontext::audioPlugin = crashName.c_str();
                const int n = audio.getNumSamples();
                if (curveSamples.exchange (0) == n && curveCount > 0 && n <= curveCapacity()) playInPieces (audio, midi);
                else inner->processBlock (audio, midi);
                crashcontext::audioPlugin = nullptr;
            }
        }
        if (t != nullptr) t->caught (midi, true);
        if (matching || match.appliedDb.load() != 0.0f) match.apply (audio, audio.getNumSamples(), matching);
        busyTicks.fetch_add (juce::Time::getHighResolutionTicks() - start, std::memory_order_relaxed);
    }

    // Bypassed, the plug-in is out of the signal flow (the audio engine's document):
    // it isn't called, its own bypass isn't trusted, and what comes in goes out
    // unchanged, MIDI too; only held back as long as the plug-in would hold it,
    // so delay compensation still lines everything up.
    void processBlockBypassed (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override
    {
        if (parked.load (std::memory_order_relaxed)) { audio.clear(); midi.clear(); return; }
        if (auto* t = tap.load (std::memory_order_relaxed)) t->caught (midi, false);   // bypassed: what it would have played
        const int n = audio.getNumSamples();
        const int passing = juce::jmin (getMainBusNumInputChannels(), getMainBusNumOutputChannels(), audio.getNumChannels());
        for (int ch = passing; ch < audio.getNumChannels(); ++ch) audio.clear (ch, 0, n);
        auto* ring = bypassRing.load (std::memory_order_acquire);
        const int delay = getLatencySamples();
        if (delay <= 0 || ring == nullptr || passing == 0) return;
        const int size = ring->getNumSamples();
        const int d = juce::jmin (delay, size - 1);
        for (int ch = 0; ch < juce::jmin (passing, ring->getNumChannels()); ++ch)
        {
            auto* x = audio.getWritePointer (ch);
            auto* r = ring->getWritePointer (ch);
            int w = bypassAt;
            for (int i = 0; i < n; ++i)
            {
                r[w] = x[i];
                x[i] = r[(w - d + size) % size];
                if (++w == size) w = 0;
            }
        }
        bypassAt = (bypassAt + n) % size;
    }

    juce::AudioProcessorParameter* getBypassParameter() const override { return nullptr; }
    void setPlayHead (juce::AudioPlayHead* h) override { AudioPluginInstance::setPlayHead (h); inner->setPlayHead (h); }
    void setNonRealtime (bool offline) noexcept override { AudioPluginInstance::setNonRealtime (offline); inner->setNonRealtime (offline); }

    double getTailLengthSeconds() const override { return inner->getTailLengthSeconds(); }
    bool acceptsMidi() const override { return inner->acceptsMidi(); }
    bool producesMidi() const override { return inner->producesMidi(); }
    bool isMidiEffect() const override { return inner->isMidiEffect(); }
    bool supportsDoublePrecisionProcessing() const override { return false; }

    // The graph never shows editors; the window asks the plug-in itself.
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    int getNumPrograms() override { return inner->getNumPrograms(); }
    int getCurrentProgram() override { return inner->getCurrentProgram(); }
    void setCurrentProgram (int i) override { inner->setCurrentProgram (i); }
    const juce::String getProgramName (int i) override { return inner->getProgramName (i); }
    void changeProgramName (int i, const juce::String& n) override { inner->changeProgramName (i, n); }
    void getStateInformation (juce::MemoryBlock& m) override { inner->getStateInformation (m); }
    void setStateInformation (const void* d, int n) override { inner->setStateInformation (d, n); }

    // The layout is the plug-in's, fixed when it was wrapped.
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l == inner->getBusesLayout(); }

private:
    juce::MidiBuffer pieceMidi, joinedMidi;

    // The block in pieces, each with its parameters' values at its start (0089).
    void playInPieces (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
    {
        const int n = audio.getNumSamples();
        joinedMidi.clear();
        int from = 0;
        while (from < n)
        {
            for (int k = 0; k < curveCount; ++k)
            {
                auto& c = curves[(size_t) k];
                const float v = c.values[(size_t) from];
                if (c.param && std::abs (c.param->getValue() - v) > 1.0e-7f) c.param->setValue (v);
            }
            int to = juce::jmin (n, from + rampPiece);
            for (int i = from + 1; i < to; ++i) if (breaks[(size_t) i] != 0) { to = i; break; }
            pieceMidi.clear();
            for (const auto m : midi)
                if (m.samplePosition >= from && m.samplePosition < to) pieceMidi.addEvent (m.data, m.numBytes, m.samplePosition - from);
            juce::AudioBuffer<float> piece (audio.getArrayOfWritePointers(), audio.getNumChannels(), from, to - from);
            inner->processBlock (piece, pieceMidi);
            for (const auto m : pieceMidi) joinedMidi.addEvent (m.data, m.numBytes, m.samplePosition + from);
            from = to;
        }
        midi.swapWith (joinedMidi);
    }


    // The delay a bypassed plug-in keeps (its latency): made on the message thread,
    // swapped in whole; the one before is kept until the next is made.
    std::unique_ptr<juce::AudioBuffer<float>> bypassOwned, bypassRetired;
    std::atomic<juce::AudioBuffer<float>*> bypassRing { nullptr };
    int bypassAt = 0;
    void makeBypassDelay()
    {
        const int want = getLatencySamples() + 1;
        const int chans = juce::jmax (1, getMainBusNumInputChannels());
        if (want <= 1 || (bypassOwned && bypassOwned->getNumSamples() >= want && bypassOwned->getNumChannels() >= chans)) return;
        auto next = std::make_unique<juce::AudioBuffer<float>> (chans, juce::nextPowerOfTwo (want));
        next->clear();
        bypassRing.store (next.get(), std::memory_order_release);
        bypassRetired = std::move (bypassOwned);
        bypassOwned = std::move (next);
    }


    std::unique_ptr<juce::AudioPluginInstance> inner;
    std::string crashName;

    static BusesProperties busesOf (const juce::AudioProcessor& p)
    {
        BusesProperties b;
        for (int i = 0; i < p.getBusCount (true); ++i)
            if (auto* bus = p.getBus (true, i)) b.addBus (true, bus->getName(), bus->getLastEnabledLayout(), bus->isEnabled());
        for (int i = 0; i < p.getBusCount (false); ++i)
            if (auto* bus = p.getBus (false, i)) b.addBus (false, bus->getName(), bus->getLastEnabledLayout(), bus->isEnabled());
        return b;
    }
};

} // namespace kal
