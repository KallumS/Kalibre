#include "ParallelGraph.h"

#include <algorithm>
#include <map>
#include <set>
#include <thread>

#if JUCE_MAC
 #include <sys/sysctl.h>
#endif

namespace kal
{
using namespace juce;
using IOProc = AudioProcessorGraph::AudioGraphIOProcessor;

namespace
{
inline void pause()
{
   #if defined (__aarch64__) || defined (__arm64__)
    __asm__ __volatile__ ("yield");
   #elif defined (__x86_64__) || defined (__i386__)
    __builtin_ia32_pause();
   #endif
}
} // namespace

//==============================================================================
struct ParallelGraph::Plan
{
    enum Kind { Processor, AudioIn, AudioOut, MidiIn, MidiOut };

    struct Input { int src = 0, srcChannel = 0, channel = 0, line = -1; };

    struct Item
    {
        Node::Ptr node;
        AudioProcessor* proc = nullptr;
        Kind kind = Processor;
        int ins = 0, outs = 0, chans = 0;
        bool producesMidi = false;
        std::vector<Input> audio;
        std::vector<int> midiFrom, next;
        int indegree = 0;
        AudioBuffer<float> buffer;
        MidiBuffer midi;
        std::vector<float*> ptrs;
    };

    // A connection made later by plug-in delay compensation: a ring as long as the delay.
    struct Line
    {
        std::vector<float> ring, out;
        size_t pos = 0;
        const float* run (const float* in, int n)
        {
            const size_t d = ring.size();
            for (int i = 0; i < n; ++i)
            {
                out[(size_t) i] = ring[pos];
                ring[pos] = in[i];
                if (++pos == d) pos = 0;
            }
            return out.data();
        }
    };

    double rate = 0;
    int block = 0, version = 0;
    std::vector<Item> items;
    std::vector<Line> lines;
    std::vector<int> roots;
    std::unique_ptr<std::atomic<int>[]> pending, slots;
    std::atomic<int> head { 0 }, tail { 0 }, remaining { 0 };
    AudioBuffer<float> out;

    // Set by the audio thread for each block, before anyone works on it.
    const AudioBuffer<float>* graphIn = nullptr;
    const MidiBuffer* graphMidi = nullptr;
    AudioPlayHead* playHead = nullptr;
    int numSamples = 0;

    std::atomic<bool> handover { true };

    void push (int i)
    {
        const int t = tail.fetch_add (1, std::memory_order_acq_rel);
        slots[(size_t) t].store (i, std::memory_order_release);
    }

    int pop()
    {
        for (;;)
        {
            int h = head.load (std::memory_order_acquire);
            if (h >= tail.load (std::memory_order_acquire)) return -1;
            if (head.compare_exchange_weak (h, h + 1, std::memory_order_acq_rel))
            {
                int v;
                while ((v = slots[(size_t) h].load (std::memory_order_acquire)) < 0) pause();
                return v;
            }
        }
    }

    void reset()
    {
        for (size_t i = 0; i < items.size(); ++i)
        {
            pending[i].store (items[i].indegree, std::memory_order_relaxed);
            slots[i].store (-1, std::memory_order_relaxed);
        }
        head.store (0, std::memory_order_relaxed);
        tail.store (0, std::memory_order_relaxed);
        remaining.store ((int) items.size(), std::memory_order_relaxed);
        for (int r : roots) push (r);
    }

    // One node, its inputs gathered first. Whoever runs it hands on what it feeds.
    void run (int index)
    {
        auto& it = items[(size_t) index];
        const int n = numSamples;
        if (it.kind == AudioIn)
        {
            for (int ch = 0; ch < it.chans; ++ch)
            {
                if (ch < graphIn->getNumChannels()) FloatVectorOperations::copy (it.ptrs[(size_t) ch], graphIn->getReadPointer (ch), n);
                else FloatVectorOperations::clear (it.ptrs[(size_t) ch], n);
            }
        }
        else
        {
            for (int ch = 0; ch < it.chans; ++ch) FloatVectorOperations::clear (it.ptrs[(size_t) ch], n);
            for (auto& in : it.audio)
            {
                const float* from = items[(size_t) in.src].buffer.getReadPointer (in.srcChannel);
                if (in.line >= 0) from = lines[(size_t) in.line].run (from, n);
                FloatVectorOperations::add (it.ptrs[(size_t) in.channel], from, n);
            }
        }
        it.midi.clear();
        for (int src : it.midiFrom) it.midi.addEvents (items[(size_t) src].midi, 0, n, 0);

        switch (it.kind)
        {
            case MidiIn:  it.midi.addEvents (*graphMidi, 0, n, 0); break;
            case AudioOut:
                for (int ch = jmin (out.getNumChannels(), it.chans); --ch >= 0;) out.addFrom (ch, 0, it.buffer, ch, 0, n);
                break;
            case Processor:
            {
                auto& p = *it.proc;
                p.setPlayHead (playHead);
                // A MIDI-only plug-in gets a buffer of no channels, as JUCE's graph gives it.
                static float* const none[1] {};
                const int chans = it.ins == 0 && it.outs == 0 ? 0 : it.chans;
                AudioBuffer<float> view (chans > 0 ? it.ptrs.data() : none, chans, n);
                if (p.isSuspended()) view.clear();
                else
                {
                    const ScopedLock lock (p.getCallbackLock());
                    if (it.node->isBypassed() && p.getBypassParameter() == nullptr) p.processBlockBypassed (view, it.midi);
                    else p.processBlock (view, it.midi);
                }
                if (! it.producesMidi) it.midi.clear();
                break;
            }
            case AudioIn: case MidiOut: break;
        }
        if (it.kind == AudioIn || it.kind == AudioOut || it.kind == MidiOut) it.midi.clear();

        for (int next : it.next)
            if (pending[(size_t) next].fetch_sub (1, std::memory_order_acq_rel) == 1) push (next);
        remaining.fetch_sub (1, std::memory_order_acq_rel);
    }
};

//==============================================================================
class ParallelGraph::Worker : public Thread
{
public:
    Worker (ParallelGraph& g, int i) : Thread ("Kalibre audio " + String (i)), owner (g), index (i) {}

    void run() override
    {
        WorkgroupToken token;
        int joined = -1;
        std::uint32_t seen = owner.generation.load();
        for (;;)
        {
            owner.generation.wait (seen);
            seen = owner.generation.load();
            if (owner.quitting.load() || threadShouldExit()) break;
            // The device's workgroup, joined again when it changes (a new device).
            if (const int v = owner.workgroupVersion.load(); v != joined)
            {
                joined = v;
                token.reset();
                std::lock_guard<std::mutex> l (owner.workgroupLock);
                if (owner.workgroup) owner.workgroup.join (token);
            }
            owner.active.fetch_add (1);
            if (owner.open.load())
                if (auto* p = owner.job.load()) owner.work (*p, p->numSamples, index);
            owner.active.fetch_sub (1);
        }
    }

private:
    ParallelGraph& owner;
    const int index;
};

//==============================================================================
int ParallelGraph::coresToUse()
{
    int cores = 0;
   #if JUCE_MAC
    // The performance cores: the efficiency ones are too slow for audio.
    int perf = 0;
    size_t size = sizeof (perf);
    if (sysctlbyname ("hw.perflevel0.physicalcpu", &perf, &size, nullptr, 0) == 0) cores = perf;
   #endif
    if (cores <= 0) cores = SystemStats::getNumPhysicalCpus();
    return jlimit (1, 12, cores);
}

ParallelGraph::ParallelGraph()
{
    startTimerHz (10);
}

ParallelGraph::~ParallelGraph()
{
    stopTimer();
    stopWorkers();
    install (nullptr);
}

void ParallelGraph::startWorkers()
{
    if (! threads.empty() || ! parallel.load()) return;
    const int count = coresToUse() - 1;   // the audio thread is one
    quitting = false;
    for (int i = 1; i <= count; ++i)
    {
        auto w = std::make_unique<Worker> (*this, i);
        if (! w->startRealtimeThread (Thread::RealtimeOptions().withPriority (10)))
            w->startThread (Thread::Priority::highest);
        threads.push_back (std::move (w));
    }
    workerCount = (int) threads.size();
}

void ParallelGraph::stopWorkers()
{
    // The audio thread stops asking for help first, and finishes the block it's in.
    workerCount = 0;
    const auto until = Time::getMillisecondCounter() + 1000;
    while (busy.load() != 0 && Time::getMillisecondCounter() < until) Thread::yield();
    quitting = true;
    generation.fetch_add (1);
    generation.notify_all();
    for (auto& t : threads) t->stopThread (2000);
    threads.clear();
}

void ParallelGraph::setParallel (bool on)
{
    parallel = on;
    if (on) startWorkers();
    else stopWorkers();
}

void ParallelGraph::setWorkgroup (AudioWorkgroup wg)
{
    {
        std::lock_guard<std::mutex> l (workgroupLock);
        workgroup = wg;
    }
    workgroupVersion.fetch_add (1);
}

//==============================================================================
void ParallelGraph::rebuild()
{
    if (MessageManager::existsAndIsCurrentThread()) refresh();
    else AudioProcessorGraph::rebuild();
}

void ParallelGraph::refresh()
{
    // The version first: a prepare from the device's thread after this makes the plan stale.
    const int version = settingsVersion.load();
    AudioProcessorGraph::rebuild();   // every node prepared for the current settings
    buildPlan (version);
}

void ParallelGraph::prepareToPlay (double rate, int block)
{
    // The plan goes first: the nodes are about to be prepared again.
    if (MessageManager::existsAndIsCurrentThread()) install (nullptr);
    else
    {
        // From the device's thread, when no block is being played: just stop using it.
        current.store (nullptr);
    }
    settingsVersion.fetch_add (1);
    AudioProcessorGraph::prepareToPlay (rate, block);
    prepared = true;
    needsPlan = true;
    if (MessageManager::existsAndIsCurrentThread()) refresh();
}

void ParallelGraph::releaseResources()
{
    settingsVersion.fetch_add (1);
    prepared = false;
    if (MessageManager::existsAndIsCurrentThread()) install (nullptr);
    else current.store (nullptr);
    AudioProcessorGraph::releaseResources();
}

void ParallelGraph::timerCallback()
{
    // JUCE rebuilds by itself too (a node added without asking): keep up.
    if (! prepared.load()) return;
    if (needsPlan.load() || currentSignature() != signature) refresh();
}

String ParallelGraph::currentSignature()
{
    String s;
    s << getSampleRate() << "/" << getBlockSize() << "|";
    for (auto* n : getNodes())
        s << (int64) n->nodeID.uid << ":" << n->getProcessor()->getLatencySamples() << (n->isBypassed() ? "b" : "") << ",";
    s << "|" << (int) getConnections().size();
    return s;
}

void ParallelGraph::install (std::unique_ptr<Plan> next)
{
    Plan* raw = next.get();
    current.store (raw);
    // The old one stays until the audio thread is done with whatever block it's in.
    const auto until = Time::getMillisecondCounter() + 1000;
    while (busy.load() != 0 && Time::getMillisecondCounter() < until) Thread::yield();
    if (busy.load() != 0) { next.release(); return; }   // stuck: better a leak than a crash
    owned = std::move (next);
}

void ParallelGraph::buildPlan (int version)
{
    needsPlan = false;
    signature = currentSignature();
    const double rate = getSampleRate();
    const int block = getBlockSize();
    if (! prepared.load() || rate <= 0 || block <= 0) { install (nullptr); return; }

    auto plan = std::make_unique<Plan>();
    plan->rate = rate;
    plan->block = block;
    plan->version = version;
    const auto& nodes = getNodes();
    const auto connections = getConnections();

    // In order: each node after everything that feeds it.
    std::map<juce::uint32, int> indexOf;
    std::vector<Node*> all;
    for (auto* n : nodes) { indexOf[n->nodeID.uid] = (int) all.size(); all.push_back (n); }
    const size_t count = all.size();
    std::vector<std::set<int>> from (count), to (count);
    for (auto& c : connections)
    {
        auto s = indexOf.find (c.source.nodeID.uid), d = indexOf.find (c.destination.nodeID.uid);
        if (s == indexOf.end() || d == indexOf.end() || s->second == d->second) continue;
        from[(size_t) d->second].insert (s->second);
        to[(size_t) s->second].insert (d->second);
    }
    std::vector<int> order, waiting (count);
    for (size_t i = 0; i < count; ++i) waiting[i] = (int) from[i].size();
    for (size_t i = 0; i < count; ++i) if (waiting[i] == 0) order.push_back ((int) i);
    for (size_t k = 0; k < order.size(); ++k)
        for (int nx : to[(size_t) order[k]])
            if (--waiting[(size_t) nx] == 0) order.push_back (nx);
    if (order.size() != count) { install (nullptr); return; }   // a loop: JUCE's graph plays it
    std::vector<int> position (count);
    for (size_t k = 0; k < order.size(); ++k) position[(size_t) order[k]] = (int) k;

    // How late each node's output is, as JUCE's graph counts it.
    std::vector<int> total (count, 0), inLatency (count, 0);
    for (int i : order)
    {
        int m = 0;
        for (int s : from[(size_t) i]) m = jmax (m, total[(size_t) s]);
        inLatency[(size_t) i] = m;
        total[(size_t) i] = m + all[(size_t) i]->getProcessor()->getLatencySamples();
    }

    plan->items.resize (count);
    for (size_t k = 0; k < count; ++k)
    {
        const int i = order[k];
        auto* node = all[(size_t) i];
        auto& it = plan->items[k];
        it.node = node;
        it.proc = node->getProcessor();
        it.ins = it.proc->getTotalNumInputChannels();
        it.outs = it.proc->getTotalNumOutputChannels();
        it.chans = jmax (it.ins, it.outs);
        it.producesMidi = it.proc->producesMidi();
        if (auto* io = dynamic_cast<IOProc*> (it.proc))
        {
            switch (io->getType())
            {
                case IOProc::audioInputNode:  it.kind = Plan::AudioIn; break;
                case IOProc::audioOutputNode: it.kind = Plan::AudioOut; break;
                case IOProc::midiInputNode:   it.kind = Plan::MidiIn; it.producesMidi = true; break;
                case IOProc::midiOutputNode:  it.kind = Plan::MidiOut; break;
                default: break;
            }
        }
        it.buffer.setSize (jmax (1, it.chans), block);
        it.buffer.clear();
        for (int ch = 0; ch < it.chans; ++ch) it.ptrs.push_back (it.buffer.getWritePointer (ch));
        it.midi.ensureSize (8192);
        for (int nx : to[(size_t) i]) it.next.push_back (position[(size_t) nx]);
        it.indegree = (int) from[(size_t) i].size();
    }
    // Connections: audio summed in, through a delay line where the paths' latencies differ.
    for (auto& c : connections)
    {
        auto s = indexOf.find (c.source.nodeID.uid), d = indexOf.find (c.destination.nodeID.uid);
        if (s == indexOf.end() || d == indexOf.end()) continue;
        auto& dst = plan->items[(size_t) position[(size_t) d->second]];
        const int src = position[(size_t) s->second];
        if (c.destination.isMIDI())
        {
            if (plan->items[(size_t) src].producesMidi || plan->items[(size_t) src].kind == Plan::MidiIn) dst.midiFrom.push_back (src);
            continue;
        }
        if (c.destination.channelIndex >= dst.chans || c.source.channelIndex >= plan->items[(size_t) src].chans) continue;
        Plan::Input in;
        in.src = src;
        in.srcChannel = c.source.channelIndex;
        in.channel = c.destination.channelIndex;
        const int delay = inLatency[(size_t) d->second] - total[(size_t) s->second];
        if (delay > 0)
        {
            Plan::Line line;
            line.ring.assign ((size_t) delay, 0.0f);
            line.out.assign ((size_t) block, 0.0f);
            in.line = (int) plan->lines.size();
            plan->lines.push_back (std::move (line));
        }
        dst.audio.push_back (in);
    }
    // Some must take turns: one after another, in order.
    if (runsAlone)
    {
        int last = -1;
        for (size_t k = 0; k < count; ++k)
        {
            auto& it = plan->items[k];
            if (it.kind != Plan::Processor || ! runsAlone (*it.proc)) continue;
            if (last >= 0)
            {
                auto& prev = plan->items[(size_t) last].next;
                if (std::find (prev.begin(), prev.end(), (int) k) == prev.end()) { prev.push_back ((int) k); ++it.indegree; }
            }
            last = (int) k;
        }
    }
    for (size_t k = 0; k < count; ++k) if (plan->items[k].indegree == 0) plan->roots.push_back ((int) k);
    plan->pending = std::make_unique<std::atomic<int>[]> (count);
    plan->slots = std::make_unique<std::atomic<int>[]> (count + 1);
    plan->out.setSize (jmax (1, getTotalNumOutputChannels()), block);
    // Rendering offline, JUCE's graph has nothing to let go of mid-render, and handing a
    // block to it would lose what the plan's delay lines hold.
    plan->handover = ! isNonRealtime();
    install (std::move (plan));
    if (parallel.load()) startWorkers();
}

String ParallelGraph::describePlan() const
{
    String s;
    auto* p = owned.get();
    if (! p) return "no plan";
    for (size_t i = 0; i < p->items.size(); ++i)
    {
        auto& it = p->items[i];
        s << (int) i << " " << it.proc->getName() << " lat " << it.proc->getLatencySamples() << " in" << it.ins << " out" << it.outs << " <-";
        for (auto& in : it.audio) s << " " << in.src << ":" << in.srcChannel << ">" << in.channel << (in.line >= 0 ? "d" + String ((int) p->lines[(size_t) in.line].ring.size()) : String());
        s << " midi";
        for (int m : it.midiFrom) s << " " << m;
        s << "\n";
    }
    return s;
}

//==============================================================================
void ParallelGraph::work (Plan& p, int, int who)
{
    threadMask.fetch_or (1u << (std::uint32_t) jmin (who, 31), std::memory_order_relaxed);
    while (p.remaining.load (std::memory_order_acquire) > 0)
    {
        const int i = p.pop();
        if (i < 0) { pause(); continue; }
        p.run (i);
    }
}

void ParallelGraph::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    busy.store (1);
    Plan* p = current.load();
    if (p == nullptr || ! parallel.load() || n > p->block || n <= 0 || p->rate != getSampleRate()
        || p->block != getBlockSize() || p->version != settingsVersion.load() || p->handover.exchange (false))
    {
        // JUCE's graph plays this one (and after a new plan, lets go of old nodes).
        busy.store (0);
        AudioProcessorGraph::processBlock (buffer, midi);
        return;
    }
    p->graphIn = &buffer;
    p->graphMidi = &midi;
    p->playHead = getPlayHead();
    p->numSamples = n;
    for (int ch = 0; ch < p->out.getNumChannels(); ++ch) FloatVectorOperations::clear (p->out.getWritePointer (ch), n);
    p->reset();

    const bool helpers = workerCount.load() > 0 && p->items.size() > 2;
    if (helpers)
    {
        job.store (p);
        open.store (true);
        generation.fetch_add (1);
        generation.notify_all();
    }
    work (*p, n, 0);
    if (helpers)
    {
        open.store (false);
        while (active.load() > 0) pause();
    }
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        if (ch < p->out.getNumChannels()) buffer.copyFrom (ch, 0, p->out, ch, 0, n);
        else buffer.clear (ch, 0, n);
    }
    midi.clear();
    threadsInLastBlock.store (__builtin_popcount (threadMask.exchange (0)));
    blocksByPlan.fetch_add (1);
    busy.store (0);
}

} // namespace kal
