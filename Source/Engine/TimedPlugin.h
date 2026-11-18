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

    // Kept in step when the plug-in's latency changes (the engine is told).
    void latencyChanged() { setLatencySamples (inner->getLatencySamples()); }

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
        match.prepare (rate, block);
    }
    void releaseResources() override { inner->releaseResources(); }

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
                inner->processBlock (audio, midi);
                crashcontext::audioPlugin = nullptr;
            }
        }
        if (t != nullptr) t->caught (midi, true);
        if (matching || match.appliedDb.load() != 0.0f) match.apply (audio, audio.getNumSamples(), matching);
        busyTicks.fetch_add (juce::Time::getHighResolutionTicks() - start, std::memory_order_relaxed);
    }

    void processBlockBypassed (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi) override
    {
        if (parked.load (std::memory_order_relaxed)) { audio.clear(); midi.clear(); return; }
        if (auto* t = tap.load (std::memory_order_relaxed)) t->caught (midi, false);   // bypassed: what it would have played
        const juce::ScopedLock lock (inner->getCallbackLock());
        inner->processBlockBypassed (audio, midi);
    }

    juce::AudioProcessorParameter* getBypassParameter() const override { return inner->getBypassParameter(); }
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
