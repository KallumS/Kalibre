/*
    ParallelGraph - the audio graph played on all the Mac's cores (decision 0078).

    JUCE's AudioProcessorGraph still holds the nodes and connections, prepares
    them and says how late each path is; only its playing is replaced. After
    every rebuild a Plan is made on the message thread: the nodes in an order
    where each comes after what feeds it, each with its own buffers, and a
    delay line on every connection that plug-in delay compensation needs,
    exactly where JUCE's graph would put one (the same sum, the same delays;
    MIDI isn't delayed, as JUCE doesn't). Then each block the audio thread and
    a few worker threads take nodes as soon as everything feeding them is
    done: two tracks' plug-ins play at the same time on two cores.

    The workers are real-time threads in the audio device's workgroup, so
    macOS schedules them as audio, on the performance cores. Nothing on the
    audio thread or the workers allocates or locks: the plan's buffers are
    made beforehand, nodes are handed out with atomics, and a worker waits for
    the next block on an atomic. A plan is swapped in with an atomic pointer,
    and the old one is kept until the audio thread is done with it.

    Nodes that share something outside themselves (the hardware insert's bus)
    are put one after another, never at the same time (`runsAlone`).

    After each new plan, one block is played by JUCE's own graph, so it lets
    go of the nodes it no longer needs (not when rendering offline: what the
    delay lines hold would be lost). Like JUCE's own new graph after a change,
    the new plan's delay lines start empty.
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace kal
{

class ParallelGraph : public juce::AudioProcessorGraph, private juce::Timer
{
public:
    ParallelGraph();
    ~ParallelGraph() override;

    // Rebuilds JUCE's graph (message thread) and makes a plan for it.
    void rebuild();

    void prepareToPlay (double rate, int block) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessorGraph::processBlock;

    // Off: the plan plays everything on the audio thread alone (still summing at
    // 64 bits, which JUCE's graph doesn't).
    void setParallel (bool);
    // Tests only: JUCE's own graph plays every block, the reference the plan is held to.
    std::atomic<bool> juceGraphOnly { false };
    bool isParallel() const { return parallel.load(); }
    // The threads besides the audio thread (0 when off or on one core).
    int workers() const { return workerCount.load(); }
    static int coresToUse();
    // How late the output is in the current plan (what reaches it), or -1 with no plan.
    int outputLatency() const;
    // How late the sound reaching each node is (by node uid), as delay compensation
    // counts it: the latest of its inputs (message thread).
    std::map<juce::uint32, int> latencyInto() const;
    // Called on the message thread after every rebuild (the engine lines automation up, 0096).
    std::function<void()> onRebuilt;

    // The audio device's workgroup, for the workers to join (device thread).
    void setWorkgroup (juce::AudioWorkgroup);

    // Nodes that must not run beside each other (the hardware insert's bus).
    std::function<bool (juce::AudioProcessor&)> runsAlone;

    // Tests: blocks played by the plan, and how many threads took part in the last one.
    std::atomic<int> blocksByPlan { 0 }, threadsInLastBlock { 0 };

    struct Plan;
    juce::String describePlan() const;   // tests: each node, its latency and delayed inputs

private:
    std::atomic<bool> parallel { true };
    std::atomic<Plan*> current { nullptr };
    std::atomic<int> busy { 0 };
    std::unique_ptr<Plan> owned;                 // the plan `current` points to (message thread)
    std::atomic<bool> needsPlan { false }, prepared { false };
    // Counts every prepare and release: a plan is used only with the settings it was made for.
    std::atomic<int> settingsVersion { 0 };
    juce::String signature;

    class Worker;
    std::vector<std::unique_ptr<Worker>> threads;
    std::atomic<int> workerCount { 0 };
    std::atomic<std::uint32_t> generation { 0 };
    std::atomic<bool> open { false }, quitting { false };
    std::atomic<int> active { 0 };
    std::atomic<Plan*> job { nullptr };
    std::atomic<int> jobSamples { 0 };
    std::atomic<std::uint32_t> threadMask { 0 };

    std::mutex workgroupLock;
    juce::AudioWorkgroup workgroup;
    std::atomic<int> workgroupVersion { 0 };

    void refresh();            // JUCE's graph rebuilt (its nodes prepared), then a plan made
    void buildPlan (int version);
    void install (std::unique_ptr<Plan>);
    void startWorkers();
    void stopWorkers();
    juce::String currentSignature();
    void timerCallback() override;
    void work (Plan&, int numSamples, int who);
    friend class Worker;
};

} // namespace kal
