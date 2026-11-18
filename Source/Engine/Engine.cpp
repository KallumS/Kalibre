#include "Engine.h"

#include "Edit.h"
#include "AudioFiles.h"
#include "TimedPlugin.h"
#include "HardwareInsert.h"
#include "Surround.h"
#include "Mp3Writer.h"
#include "NoteSplitter.h"

namespace kal
{
using namespace juce;
using IOProc = AudioProcessorGraph::AudioGraphIOProcessor;

Engine::Engine (PluginManager& pm) : pluginManager (pm)
{
    syncInput = [this] (const String& device, const MidiMessage& m) { syncDevices.input (device, m); };
    graph.setPlayHead (this);
    // The hardware insert shares the interface's bus: one at a time (decision 0078).
    graph.runsAlone = [] (AudioProcessor& p)
    {
        if (auto* t = dynamic_cast<TimedPlugin*> (&p)) return dynamic_cast<HardwareInsert*> (&t->plugin()) != nullptr;
        return dynamic_cast<HardwareInsert*> (&p) != nullptr;
    };
    tempo.post (std::make_unique<TempoMap>());
    // The graph's own ends exist from the start, so a project can be synced
    // (and rendered) with no audio device at all. Its channels are set first:
    // a connection to a channel the end doesn't have yet is refused.
    graph.setPlayConfigDetails (2, 2, rate, blockSize);
    audioIn = graph.addNode (std::make_unique<IOProc> (IOProc::audioInputNode));
    audioOut = graph.addNode (std::make_unique<IOProc> (IOProc::audioOutputNode));
    midiIn = graph.addNode (std::make_unique<IOProc> (IOProc::midiInputNode));
    metronomeNode = graph.addNode (std::make_unique<Metronome> (shared));
    player.setProcessor (&graph);   // ready for whatever sound card calls it
    startTimerHz (20);
}

Engine::~Engine()
{
    stopTimer();
    deviceManager.removeAudioCallback (this);
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    player.setProcessor (nullptr);
    araSessions.clear();
    for (auto& [id, node] : allDevices)
        if (auto* p = pluginOf (node)) p->removeListener (this);
    graph.clear();
}

String Engine::startAudio (bool withInputs)
{
    auto settingsFile = File::getSpecialLocation (File::userApplicationDataDirectory)
                       #if JUCE_MAC
                           .getChildFile ("Application Support")
                       #endif
                           .getChildFile ("Kalibre/Audio.xml");
    std::unique_ptr<XmlElement> saved = parseXML (settingsFile);
    auto error = deviceManager.initialise (withInputs ? 2 : 0, 2, saved.get(), true);

    // Every MIDI keyboard plugged in is listened to.
    for (auto& d : MidiInput::getAvailableDevices())
        deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
    // Every message comes here first: learned controllers, control surfaces and
    // sync are taken out; the rest is played into the tracks.
    deviceManager.addMidiInputDeviceCallback ({}, this);

    player.setProcessor (&graph);
    deviceManager.addAudioCallback (this);
    shape.clear();   // rewire with the device's inputs known
    return error;
}

//==============================================================================
void Engine::audioDeviceAboutToStart (AudioIODevice* device)
{
    rate = device->getCurrentSampleRate();
    blockSize = device->getCurrentBufferSizeSamples();
    startedInputs = device->getActiveInputChannels().countNumberOfSetBits();
    graph.setWorkgroup (device->getWorkgroup());   // the workers are audio threads too (decision 0078)
    shared.clock.rate = rate;
    cache.setSampleRate (rate);
    player.audioDeviceAboutToStart (device);
    // The interface's channels may have changed: wire the outputs again.
    MessageManager::callAsync ([this, alive = std::weak_ptr<bool> (aliveToken)]
    {
        if (alive.expired()) return;
        shape.clear();
        if (onAudioReady) onAudioReady();
    });
}

void Engine::audioDeviceStopped()
{
    player.audioDeviceStopped();
}

void Engine::audioDeviceIOCallbackWithContext (const float* const* ins, int numIns, float* const* outs, int numOuts,
                                               int n, const AudioIODeviceCallbackContext& context)
{
    const auto callbackStart = Time::getHighResolutionTicks();
    auto& c = shared.clock;
    const auto* map = tempo.take();
    double p = pos.load();
    c.jumped = false;
    // seekTo holds noJump unless a jump is asked for. Positions can be below
    // zero (a count-in starts before the song), so "none" is far below any.
    if (const double s = seekTo.exchange (noJump); s > noJump / 2)
    {
        p = s;
        c.jumped = true;
    }
    c.playing = playing.load();
    // With others in a Link session, playing starts on the session's next bar.
    const int quantum = map ? map->meterAt (map->tickAt (p)).numerator * 4 / map->meterAt (map->tickAt (p)).denominator : 4;
    if (c.playing && waitingForLink.load() && map)
    {
        if (syncDevices.holdStartForLink (*map, jmax (1, quantum))) c.playing = false;
        else waitingForLink = false;
    }
    c.recording = recording.load();
    c.start = p;
    c.samples = n;
    c.rate = rate;
    c.wrapAt = -1;
    double next = p;
    if (c.playing)
    {
        next = p + n / rate;
        const double le = loopEnd.load(), ls = loopStart.load();
        if (loopOn.load() && le > ls && p < le && next >= le)
        {
            c.wrapAt = jlimit (0, n - 1, (int) ((le - p) * rate));
            c.wrapTo = ls;
            next = ls + (n - c.wrapAt) / rate;
        }
    }
    if (map)
    {
        const double ticks = map->ticksAt (p);
        c.ppq = ticks / PPQ;
        c.bpm = map->bpmAt ((Tick) ticks);
        const auto& m = map->meterAt ((Tick) ticks);
        info.setBpm (c.bpm);
        info.setTimeSignature (AudioPlayHead::TimeSignature { m.numerator, m.denominator });
        info.setPpqPosition (c.ppq);
        const int bar = map->barAt ((Tick) ticks);
        info.setPpqPositionOfLastBarStart ((double) map->barStart (bar) / PPQ);
        info.setBarCount (bar);
        info.setTimeInSeconds (p);
        info.setTimeInSamples ((int64) (p * rate));
        info.setIsPlaying (c.playing);
        info.setIsRecording (c.recording && c.playing);
        info.setIsLooping (loopOn.load());
        info.setLoopPoints (AudioPlayHead::LoopPoints { map->ticksAt (loopStart.load()) / PPQ, map->ticksAt (loopEnd.load()) / PPQ });
        shared.songTicks = c.playing ? ticks : -1.0;
    }
    // Hardware inserts (decision 0040) read the inputs and add to the outputs.
    auto& hw = HardwareBus::get();
    hw.inputs = ins;
    hw.numInputs = numIns;
    hw.numOutputs = numOuts;
    hw.samples = n;
    hw.live = true;
    if (hw.sends.getNumChannels() < numOuts || hw.sends.getNumSamples() < n) hw.sends.setSize (jmax (numOuts, 2), jmax (n, 4096), false, false, true);
    hw.sends.clear();
    player.audioDeviceIOCallbackWithContext (ins, numIns, outs, numOuts, n, context);
    for (int ch = 0; ch < numOuts; ++ch)
        if (outs[ch]) FloatVectorOperations::add (outs[ch], hw.sends.getReadPointer (ch), n);
    hw.live = false;
    if (map)
    {
        // Sync messages are timed to when this block is heard.
        double latencyMs = 0;
        if (auto* d = deviceManager.getCurrentAudioDevice()) latencyMs = d->getOutputLatencyInSamples() * 1000.0 / rate;
        syncDevices.block (c, *map, Time::getMillisecondCounterHiRes() + latencyMs, jmax (1, quantum));
    }
    if (c.playing) pos.store (next);
    else pos.store (p);
    // A block that took longer than it lasts is a dropout (decision 0038).
    if (Time::highResolutionTicksToSeconds (Time::getHighResolutionTicks() - callbackStart) > n / rate) ++late;
}

//==============================================================================
void Engine::play()
{
    if (countInSeconds > 0 && ! playing)
    {
        // A count-in starts that much earlier with the clicks forced on.
        if (auto* m = dynamic_cast<Metronome*> (metronomeNode ? metronomeNode->getProcessor() : nullptr))
            m->countInUntil = pos.load();
        seekTo = pos.load() - countInSeconds;
    }
    waitingForLink = syncDevices.linkOn() && syncDevices.linkPeers() > 0;
    playing = true;
}

void Engine::stop()
{
    playing = false;
    recording = false;
}

void Engine::setRecording (bool r) { recording = r; }

void Engine::locate (double seconds)
{
    if (playing) seekTo = seconds;
    else { pos = seconds; seekTo = seconds; }
}

double Engine::heardPosition()
{
    return playing ? pos.load() - graph.getLatencySamples() / rate : pos.load();
}

void Engine::setLoop (bool on, double start, double end)
{
    loopStart = start;
    loopEnd = end;
    loopOn = on;
}

int Engine::latencySamples() { return graph.getLatencySamples(); }

//==============================================================================
void Engine::setMappings (std::vector<MidiMapping> maps)
{
    const std::lock_guard<std::mutex> lock (mappingLock);
    mappings = std::move (maps);
}

void Engine::handleIncomingMidiMessage (MidiInput* source, const MidiMessage& m)
{
    if (m.isActiveSense()) return;
    const String from = source ? source->getIdentifier() : String();
    // A control surface's own port (decision 0034).
    if (surfaceInput && surfaceInput (from, m)) return;
    // Sync from another device (decision 0033).
    if (m.isMidiClock() || m.isMidiStart() || m.isMidiStop() || m.isMidiContinue() || m.isSongPositionPointer()
        || m.isQuarterFrame() || m.isFullFrame()
        || (m.isSysEx() && m.getSysExDataSize() >= 4 && m.getSysExData()[0] == 0x7F && m.getSysExData()[2] == 0x06))   // MMC
    {
        if (syncInput) syncInput (from, m);
        return;
    }
    if (m.getRawDataSize() > 3) return;
    std::uint8_t bytes[3] {};
    for (int i = 0; i < m.getRawDataSize(); ++i) bytes[i] = m.getRawData()[i];

    // MIDI learn (decision 0032): the next controller is learned; learned ones move their control.
    {
        int channel = 0, controller = 0;
        if (learning.load() && midimap::keyOf (bytes, channel, controller))
        {
            learning = false;
            MessageManager::callAsync ([this, channel, controller] { if (onLearned) onLearned (channel, controller); });
            return;
        }
        const std::lock_guard<std::mutex> lock (mappingLock);
        const int index = midimap::match (mappings, bytes);
        if (index >= 0)
        {
            const auto map = mappings[(size_t) index];
            const double value = midimap::normalised (bytes);
            MessageManager::callAsync ([this, map, value] { if (onMapped) onMapped (map, value); });
            return;
        }
    }

    // Step input (decision 0076): the window writes the notes; they're still heard.
    if (stepInputOn.load() && (m.isNoteOn() || m.isNoteOff()))
    {
        const int pitch = m.getNoteNumber(), velocity = m.getVelocity();
        const bool on = m.isNoteOn();
        MessageManager::callAsync ([this, pitch, velocity, on] { if (onStepKey) onStepKey (pitch, velocity, on); });
    }

    auto queued = m;
    queued.setTimeStamp (Time::getMillisecondCounterHiRes() * 0.001);
    player.getMidiMessageCollector().addMessageToQueue (queued);

    capture::Event e;
    e.wallSeconds = Time::getMillisecondCounterHiRes() / 1000.0;
    e.songTicks = shared.songTicks.load();
    for (int i = 0; i < m.getRawDataSize(); ++i) e.bytes[i] = m.getRawData()[i];
    shared.midiCapture.push (e);
}

void Engine::audition (Id track, int pitch, int velocity, double seconds)
{
    auto it = tracks.find (track);
    if (it == tracks.end() || ! it->second.source) return;
    auto& collector = player.getMidiMessageCollector();
    ignoreUnused (collector);
    // The track's source passes on whatever arrives at the MIDI input node only
    // for the live track, so an audition goes through the collector to the live one,
    // which the window makes the track being edited.
    auto on = MidiMessage::noteOn (1, pitch, (uint8) jlimit (1, 127, velocity));
    on.setTimeStamp (Time::getMillisecondCounterHiRes() * 0.001);
    player.getMidiMessageCollector().addMessageToQueue (on);
    Timer::callAfterDelay ((int) (seconds * 1000), [this, pitch]
    {
        auto off = MidiMessage::noteOff (1, pitch);
        off.setTimeStamp (Time::getMillisecondCounterHiRes() * 0.001);
        player.getMidiMessageCollector().addMessageToQueue (off);
    });
}

//==============================================================================
Id Engine::deviceIdOf (AudioProcessor* p) const
{
    for (auto& [id, node] : deviceNodes)
        if (node && pluginOf (node) == p) return id;
    return 0;
}

void Engine::touched (AudioProcessor* p, int index, float value, int gesture)
{
    auto send = [this, p, index, value, gesture]
    {
        const Id dev = deviceIdOf (p);
        if (dev == 0 || ! onTouched) return;
        Touch t;
        t.device = dev;
        t.param = index;
        t.value = value;
        t.gesture = gesture;
        if (auto* prm = p->getParameters()[index]) t.name = prm->getName (64);
        onTouched (t);
    };
    if (MessageManager::existsAndIsCurrentThread()) send();
    else MessageManager::callAsync (send);
}

void Engine::audioProcessorParameterChanged (AudioProcessor* p, int index, float value) { touched (p, index, value, 0); }
void Engine::audioProcessorParameterChangeGestureBegin (AudioProcessor* p, int index)
{
    touched (p, index, p->getParameters()[index] ? p->getParameters()[index]->getValue() : 0.0f, 1);
}
void Engine::audioProcessorParameterChangeGestureEnd (AudioProcessor* p, int index)
{
    touched (p, index, p->getParameters()[index] ? p->getParameters()[index]->getValue() : 0.0f, 2);
}

void Engine::audioProcessorChanged (AudioProcessor* p, const ChangeDetails& d)
{
    if (d.latencyChanged)
        MessageManager::callAsync ([this, p]
        {
            // The wrapper takes on the plug-in's new latency before the graph lines things up again.
            for (auto& [id, node] : deviceNodes)
                if (pluginOf (node) == p)
                    if (auto* t = dynamic_cast<TimedPlugin*> (node->getProcessor())) t->latencyChanged();
            graph.rebuild();
        });
    if (d.nonParameterStateChanged && onDevicePluginChanged)
        MessageManager::callAsync ([this] { if (onDevicePluginChanged) onDevicePluginChanged(); });
}

void Engine::bury (Node::Ptr node)
{
    if (! node) return;
    graph.removeNode (node->nodeID);
    graveyard.push_back ({ node, Time::getMillisecondCounter() });
}

void Engine::timerCallback()
{
    if (++loadCountdown >= 10) { loadCountdown = 0; measureLoad(); }
    const auto now = Time::getMillisecondCounter();
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [now] (const auto& g) { return now - g.second > 1500; }),
                     graveyard.end());
    for (auto& [id, t] : tracks)
        if (auto* s = dynamic_cast<TrackSource*> (t.source ? t.source->getProcessor() : nullptr)) s->data.collect();
    tempo.collect();
}

//==============================================================================
AudioPluginInstance* Engine::pluginOf (const Node::Ptr& node)
{
    if (! node) return nullptr;
    if (auto* t = dynamic_cast<TimedPlugin*> (node->getProcessor())) return &t->plugin();
    return dynamic_cast<AudioPluginInstance*> (node->getProcessor());
}

AudioPluginInstance* Engine::instance (Id device) const
{
    auto it = deviceNodes.find (device);
    return it == deviceNodes.end() ? nullptr : pluginOf (it->second);
}

void Engine::measureLoad()
{
    // Each track's plug-ins' share of real time since the last look (decision 0038).
    const auto now = Time::getHighResolutionTicks();
    const double elapsed = Time::highResolutionTicksToSeconds (now - lastLoadTicks);
    lastLoadTicks = now;
    if (elapsed <= 0) return;
    std::map<Id, double> load;
    for (auto& [track, devices] : trackDevices)
    {
        double busy = 0;
        for (auto d : devices)
        {
            auto it = deviceNodes.find (d);
            if (it == deviceNodes.end() || ! it->second) continue;
            if (auto* t = dynamic_cast<TimedPlugin*> (it->second->getProcessor()))
            {
                const auto total = t->busyTicks.load();
                busy += Time::highResolutionTicksToSeconds (total - lastBusy[d]);
                lastBusy[d] = total;
            }
        }
        load[track] = busy / elapsed;
    }
    trackLoad = std::move (load);
}

float Engine::matchingDb (Id device) const
{
    auto it = deviceNodes.find (device);
    if (it == deviceNodes.end() || ! it->second) return 0;
    auto* tp = dynamic_cast<TimedPlugin*> (it->second->getProcessor());
    return tp ? tp->match.appliedDb.load() : 0.0f;
}

double Engine::trackCpu (Id track) const
{
    auto it = trackLoad.find (track);
    return it == trackLoad.end() ? 0.0 : it->second;
}

int Engine::dropouts() const
{
    int xruns = 0;
    if (auto* d = const_cast<AudioDeviceManager&> (deviceManager).getCurrentAudioDevice()) xruns = jmax (0, d->getXRunCount());
    return late.load() + xruns - dropoutBase;
}

void Engine::resetDropouts()
{
    late = 0;
    dropoutBase = 0;
    dropoutBase = dropouts();
}

Fader* Engine::fader (Id track) const
{
    auto it = tracks.find (track);
    return it == tracks.end() || ! it->second.fader ? nullptr : dynamic_cast<Fader*> (it->second.fader->getProcessor());
}

TrackSource* Engine::recordSource (Id track) const
{
    auto it = tracks.find (track);
    if (it != tracks.end() && it->second.recorder) return dynamic_cast<TrackSource*> (it->second.recorder->getProcessor());
    return source (track);
}

TrackSource* Engine::source (Id track) const
{
    auto it = tracks.find (track);
    return it == tracks.end() || ! it->second.source ? nullptr : dynamic_cast<TrackSource*> (it->second.source->getProcessor());
}

std::string Engine::araStateOf (Id device) const
{
    auto it = araSessions.find (device);
    return it == araSessions.end() || ! it->second ? std::string() : it->second->archive();
}

bool Engine::playedByAra (Id track) const { return araTracks.count (track) > 0; }

std::string Engine::stateOf (Id device, Id version) const
{
    auto it = allDevices.find ({ version, device });
    auto* inst = it == allDevices.end() ? nullptr : pluginOf (it->second);
    if (! inst) return {};
    MemoryBlock m;
    inst->getStateInformation (m);
    return m.toBase64Encoding().toStdString();
}

std::string Engine::stateOf (Id device) const
{
    auto* p = instance (device);
    if (! p) return {};
    MemoryBlock m;
    p->getStateInformation (m);
    return m.toBase64Encoding().toStdString();
}

// A plug-in with a second input (a sidechain) gets it switched on, stereo if
// it takes stereo, otherwise mono (decision 0031).
bool Engine::enableSidechain (AudioProcessor& p)
{
    if (p.getBusCount (true) < 2) return false;
    auto layout = p.getBusesLayout();
    for (auto set : { AudioChannelSet::stereo(), AudioChannelSet::mono() })
    {
        layout.inputBuses.getReference (1) = set;
        if (p.checkBusesLayoutSupported (layout) && p.setBusesLayout (layout)) return true;
    }
    return false;
}

int Engine::sidechainChannels (Id device) const
{
    auto* p = instance (device);
    if (! p || p->getBusCount (true) < 2) return 0;
    if (auto* bus = p->getBus (true, 1)) return bus->isEnabled() ? bus->getNumberOfChannels() : 0;
    return 0;
}

Engine::Node::Ptr Engine::nodeForDevice (const Device& d, Id version, String& error)
{
    const auto key = d.plugin.format + "|" + d.plugin.identifier + "|" + std::to_string (d.plugin.uid);
    const VersionedId vid { version, d.id };
    if (auto it = allDevices.find (vid); it != allDevices.end() && allKeys[vid] == key)
        return it->second;
    auto instance = pluginManager.create (d.plugin, rate, blockSize, error);
    if (! instance) return nullptr;
    if (! d.state.empty())
    {
        MemoryBlock m;
        if (m.fromBase64Encoding (d.state)) instance->setStateInformation (m.getData(), (int) m.getSize());
    }
    enableSidechain (*instance);
    // An instrument with more outputs than two has them all switched on (decision 0040).
    if (d.role == DeviceRole::Instrument)
        for (int i = 1; i < instance->getBusCount (false); ++i)
            if (auto* bus = instance->getBus (false, i)) bus->enable (true);
    instance->addListener (this);
    auto node = graph.addNode (std::make_unique<TimedPlugin> (std::move (instance)));
    if (auto old = allDevices[vid]) { if (auto* op = pluginOf (old)) op->removeListener (this); bury (old); }
    allDevices[vid] = node;
    allKeys[vid] = key;
    return node;
}

void Engine::sync (const Project& project, Id live, const File& folder)
{
    using Kind = AudioProcessorGraph::UpdateKind;
    liveTrack = live;
    tempoCopy = project.tempo;
    tempo.post (std::make_unique<TempoMap> (project.tempo));
    if (auto* m = dynamic_cast<Metronome*> (metronomeNode ? metronomeNode->getProcessor() : nullptr))
    {
        auto s = std::make_unique<Metronome::Settings>();
        s->m = project.metronome;
        s->tempo = project.tempo;
        m->settings.post (std::move (s));
    }
    if (! audioIn) return;

    std::vector<const Track*> all;
    for (auto& t : project.tracks) all.push_back (&t);
    all.push_back (&project.master);
    for (auto& c : project.cue) all.push_back (&c);   // after the master, heard but not exported (decision 0051)
    const Track* cue = project.cue.empty() || rendering ? nullptr : &project.cue.front();

    // Nodes for every track, and for every active device: the playing
    // version's wired in, the others' parked, loaded and silent (decision 0050).
    std::set<Id> wantedTracks, wantedDevices;
    std::set<VersionedId> wantedNodes;
    std::map<Id, Node::Ptr> playing;
    auto park = [] (const Node::Ptr& node, bool parked)
    {
        if (auto* tp = dynamic_cast<TimedPlugin*> (node->getProcessor())) tp->parked = parked;
    };
    trackDevices.clear();
    for (auto* t : all)
    {
        wantedTracks.insert (t->id);
        for (auto& d : t->devices) trackDevices[t->id].push_back (d.id);
        auto& nodes = tracks[t->id];
        if (! nodes.source) nodes.source = graph.addNode (std::make_unique<TrackSource> (shared, t->id), {}, Kind::none);
        if (! nodes.fader) nodes.fader = graph.addNode (std::make_unique<Fader>(), {}, Kind::none);
        if (t->kind == TrackKind::Summing && ! nodes.recorder)
            nodes.recorder = graph.addNode (std::make_unique<TrackSource> (shared, t->id), {}, Kind::none);
        if (t->frozen) continue;   // a frozen track's plug-ins are unloaded
        const Id active = t->version().id;
        for (auto& d : t->devices)
        {
            if (! d.active) continue;
            String error;
            if (auto node = nodeForDevice (d, active, error))
            {
                wantedDevices.insert (d.id);
                wantedNodes.insert ({ active, d.id });
                playing[d.id] = node;
                park (node, false);
                if (auto* tp = dynamic_cast<TimedPlugin*> (node->getProcessor())) tp->matchLoudness = d.matchLoudness;
            }
            else if (onMessage) onMessage (String (d.plugin.name) + " couldn't be loaded. " + error);
        }
        for (size_t v = 0; v < t->versions.size(); ++v)
        {
            const auto& version = t->versions[v];
            if ((int) v == t->activeVersion) continue;
            for (auto& d : version.devices)
            {
                if (! d.active) continue;
                String error;
                if (auto node = nodeForDevice (d, version.id, error))
                {
                    wantedNodes.insert ({ version.id, d.id });
                    park (node, true);
                }
            }
        }
    }
    for (auto it = tracks.begin(); it != tracks.end();)
    {
        if (wantedTracks.count (it->first)) { ++it; continue; }
        bury (it->second.source);
        bury (it->second.fader);
        bury (it->second.recorder);
        for (auto& s : it->second.sends) bury (s.second);
        it = tracks.erase (it);
    }
    // An ARA document goes before its plug-in, and with it when another version's takes over.
    for (auto it = araSessions.begin(); it != araSessions.end();)
    {
        const auto p = playing.find (it->first);
        if (p != playing.end() && pluginOf (p->second) == araOwner[it->first]) { ++it; continue; }
        araOwner.erase (it->first);
        it = araSessions.erase (it);
    }
    for (auto it = allDevices.begin(); it != allDevices.end();)
    {
        if (wantedNodes.count (it->first)) { ++it; continue; }
        if (auto* op = pluginOf (it->second)) op->removeListener (this);
        bury (it->second);
        allKeys.erase (it->first);
        it = allDevices.erase (it);
    }
    deviceNodes = playing;

    // Solo: a track is heard when nothing is soloed, or it is, or a folder
    // around it is, or something inside it is. Returns are always heard.
    std::set<Id> soloed;
    for (auto* t : all) if (t->solo) soloed.insert (t->id);
    const bool anySolo = ! soloed.empty();
    auto heard = [&] (const Track& t)
    {
        if (t.mute) return false;
        if (! anySolo || t.kind == TrackKind::Master || t.kind == TrackKind::Return || t.isCue()) return true;
        if (soloed.count (t.id)) return true;
        for (auto id : soloed) if (project.isInside (t.id, id) || project.isInside (id, t.id)) return true;
        // Something soloed sends to... no: only routing up the tree counts.
        return false;
    };

    // The wiring, as text: rebuilt only when it changes.
    String newShape;
    const int busChannels = surround::channels (project.surround);
    const bool surroundMix = busChannels > 2;
    // Rendering one track (freeze, bounce, decision 0080): only it goes out, straight from its fader.
    const Id only = rendering ? renderOnlyTrack : 0;
    newShape << "fmt" << busChannels << "/" << graph.getTotalNumOutputChannels() << (cue ? "cue" : "") << "only" << (int64) only << ";";
    for (auto* t : all)
    {
        if (t->sourceDevice != 0)
            if (auto it = deviceNodes.find (t->sourceDevice); it != deviceNodes.end() && it->second)
                newShape << "src" << (int64) t->sourceDevice << ":" << t->sourcePair << ":" << (int64) it->second->nodeID.uid << ";";
        newShape << (int64) t->id << ":" << (int) t->kind << ":" << (int64) project.outputOf (t->id) << ":" << (t->id == live ? 1 : 0)
                 << (t->armed ? "a" + String ((int) t->armFor) : String()) << (int) t->monitor << "/" << t->input.audioChannel << "[";
        // The node's own number too: a device replaced by another under the same id is rewired.
        for (auto& d : t->devices) if (d.active && wantedDevices.count (d.id)) newShape << (int64) d.id << "#" << (int64) deviceNodes[d.id]->nodeID.uid << (int) d.role << (d.bypassed ? "b" : "") << "<" << (int64) d.sidechainFrom << ",";
        for (auto& d : t->devices) if (! d.active && d.role == DeviceRole::AudioFx && d.offLatency > 0) newShape << "off" << (int64) d.id << ":" << heldSamples (d) << ",";
        newShape << "]{";
        // The Note Splitter's outputs (decision 0083).
        for (auto& d : t->devices)
            if (! d.midiTo.empty() && d.active && wantedDevices.count (d.id))
            {
                newShape << "split" << (int64) d.id;
                for (auto to : d.midiTo) newShape << ">" << (int64) to;
            }
        for (auto& s : t->sends) newShape << (int64) s.target << (s.preFader ? "p" : "") << ",";
        newShape << "}";
    }
    if (newShape != shape)
    {
        shape = newShape;
        for (auto& c : graph.getConnections()) graph.removeConnection (c, Kind::none);

        auto connectAudio = [&] (NodeID from, NodeID to)
        {
            for (int ch = 0; ch < 2; ++ch) graph.addConnection ({ { from, ch }, { to, ch } }, Kind::none);
        };
        auto connectChannels = [&] (NodeID from, NodeID to, int count)
        {
            for (int ch = 0; ch < count; ++ch) graph.addConnection ({ { from, ch }, { to, ch } }, Kind::none);
        };
        auto connectMidi = [&] (NodeID from, NodeID to)
        {
            graph.addConnection ({ { from, AudioProcessorGraph::midiChannelIndex }, { to, AudioProcessorGraph::midiChannelIndex } }, Kind::none);
        };

        // Where sound going into a track goes: its first audio effect, or its fader.
        std::set<Id> keepHeld;
        std::map<Id, NodeID> entry;
        std::map<Id, NodeID> preFader;
        for (auto* t : all)
        {
            auto& nodes = tracks[t->id];
            std::vector<NodeID> fx;
            for (auto& d : t->devices)
            {
                if (d.active && d.role == DeviceRole::AudioFx && wantedDevices.count (d.id)) fx.push_back (deviceNodes[d.id]->nodeID);
                // A switched-off effect's delay stands in its place (0086).
                if (! d.active && d.role == DeviceRole::AudioFx && heldSamples (d) > 0)
                {
                    auto& held = heldDelays[d.id];
                    auto* now = held ? dynamic_cast<HeldDelay*> (held->getProcessor()) : nullptr;
                    if (! now || now->length != heldSamples (d))
                    {
                        if (held) bury (held);
                        held = graph.addNode (std::make_unique<HeldDelay> (heldSamples (d)), {}, Kind::none);
                    }
                    keepHeld.insert (d.id);
                    fx.push_back (held->nodeID);
                }
            }
            entry[t->id] = fx.empty() ? nodes.fader->nodeID : fx.front();
            for (size_t i = 0; i + 1 < fx.size(); ++i) connectAudio (fx[i], fx[i + 1]);
            if (! fx.empty()) connectAudio (fx.back(), nodes.fader->nodeID);
            preFader[t->id] = fx.empty() ? nodes.source->nodeID : fx.back();
        }

        const int inputs = deviceManager.getCurrentAudioDevice()
                             ? deviceManager.getCurrentAudioDevice()->getActiveInputChannels().countNumberOfSetBits()
                             : startedInputs.load();   // a device started without the manager (the tests')
        for (auto* t : all)
        {
            auto& nodes = tracks[t->id];
            const auto src = nodes.source->nodeID;
            // Live input.
            // Armed for audio, the sound coming in; armed for MIDI, not (decision 0064).
            const bool armedAudio = t->armedAudio();
            const bool monitorIn = t->monitor == Monitor::In || (armedAudio && t->monitor == Monitor::Auto);
            if ((armedAudio || monitorIn) && t->input.audioChannel >= 0 && inputs > 0)
                for (int ch = 0; ch < 2; ++ch)
                {
                    const int inCh = jmin (inputs - 1, t->input.audioChannel + (t->input.stereo ? ch : 0));
                    graph.addConnection ({ { audioIn->nodeID, inCh }, { src, ch } }, Kind::none);
                }
            if ((t->id == live || t->armed) && ! t->isCue() && t->kind != TrackKind::Summing) connectMidi (midiIn->nodeID, src);
            // A Summing track's recorder hears the master's first two channels (decision 0052).
            if (nodes.recorder)
                for (int ch = 0; ch < 2; ++ch)
                    graph.addConnection ({ { tracks[project.master.id].fader->nodeID, ch }, { nodes.recorder->nodeID, ch } }, Kind::none);

            // MIDI effects in a line, then every instrument fed from the last.
            NodeID midiFrom = src;
            std::vector<NodeID> instruments;
            for (auto& d : t->devices)
            {
                if (! d.active || ! wantedDevices.count (d.id)) continue;
                const auto id = deviceNodes[d.id]->nodeID;
                deviceNodes[d.id]->setBypassed (d.bypassed);
                if (d.role == DeviceRole::MidiFx) { connectMidi (midiFrom, id); midiFrom = id; }
                else if (d.role == DeviceRole::Instrument) instruments.push_back (id);
            }
            for (auto id : instruments)
            {
                connectMidi (midiFrom, id);
                connectAudio (id, entry[t->id]);
            }
            // The clips' own audio joins the instruments' (audio and MIDI on one track).
            connectAudio (src, entry[t->id]);
            // Audio effects that want MIDI (a vocoder, a gate) get the notes too.
            for (auto& d : t->devices)
                if (d.active && d.role == DeviceRole::AudioFx && wantedDevices.count (d.id)
                    && deviceNodes[d.id]->getProcessor()->acceptsMidi())
                    connectMidi (midiFrom, deviceNodes[d.id]->nodeID);

            // Sidechains: another track's sound, after its fader, into a plug-in's second input.
            for (auto& d : t->devices)
            {
                if (! d.active || d.sidechainFrom == 0 || ! wantedDevices.count (d.id)) continue;
                const auto from = tracks.find (d.sidechainFrom);
                if (from == tracks.end() || ! from->second.fader || ! edit::canRoute (project, d.sidechainFrom, t->id)) continue;
                auto* proc = deviceNodes[d.id]->getProcessor();
                const int sc = sidechainChannels (d.id);
                if (sc <= 0) continue;
                const int first = proc->getChannelIndexInProcessBlockBuffer (true, 1, 0);
                for (int ch = 0; ch < sc; ++ch)
                    graph.addConnection ({ { from->second.fader->nodeID, jmin (ch, 1) }, { deviceNodes[d.id]->nodeID, first + ch } }, Kind::none);
                if (sc == 1)   // a mono sidechain hears both sides
                    graph.addConnection ({ { from->second.fader->nodeID, 1 }, { deviceNodes[d.id]->nodeID, first } }, Kind::none);
            }

            // Out: to its folder, the track it is routed to, or the master. In a
            // surround mix every channel goes; a bus's plug-ins take the front two
            // and the rest go straight to its fader (decision 0040).
            // The master goes out through the Cue track when there is one, except
            // when exporting (decision 0051); the Cue track goes out.
            const bool toOut = t->isCue() || (t->kind == TrackKind::Master && cue == nullptr);
            if (only != 0 && t->id == only)
            {
                // Rendering one track: it alone goes out. The rest are wired as ever, the
                // master to nowhere, so an offline plan plays only what this track hears:
                // a folder or return feeding one of its sidechains, and what feeds those.
                connectChannels (nodes.fader->nodeID, audioOut->nodeID, jmin (busChannels, graph.getTotalNumOutputChannels()));
            }
            else if (toOut)
            {
                if (only == 0 && ! (t->isCue() && cue == nullptr))
                    connectChannels (nodes.fader->nodeID, audioOut->nodeID, jmin (busChannels, graph.getTotalNumOutputChannels()));
            }
            else
            {
                const Id out = project.outputOf (t->id);
                const Id target = t->kind == TrackKind::Master ? cue->id : out == 0 ? project.master.id : out;
                const auto targetEntry = entry[target];
                const auto targetFader = tracks[target].fader->nodeID;
                if (targetEntry == targetFader) connectChannels (nodes.fader->nodeID, targetFader, busChannels);
                else
                {
                    connectAudio (nodes.fader->nodeID, targetEntry);
                    for (int ch = 2; ch < busChannels; ++ch) graph.addConnection ({ { nodes.fader->nodeID, ch }, { targetFader, ch } }, Kind::none);
                }
            }

            // An instrument's further outputs, each on a track of its own.
            if (t->sourceDevice != 0)
                if (auto it = deviceNodes.find (t->sourceDevice); it != deviceNodes.end() && it->second)
                {
                    const int first = t->sourcePair * 2;
                    if (first + 1 < it->second->getProcessor()->getTotalNumOutputChannels())
                        for (int ch = 0; ch < 2; ++ch)
                            graph.addConnection ({ { it->second->nodeID, first + ch }, { entry[t->id], ch } }, Kind::none);
                }

            // Sends.
            std::set<Id> keep;
            for (auto& s : t->sends)
            {
                if (only != 0 && t->id == only) break;
                if (! project.track (s.target) || ! edit::canRoute (project, t->id, s.target)) continue;
                keep.insert (s.target);
                auto& sendNode = nodes.sends[s.target];
                if (! sendNode) sendNode = graph.addNode (std::make_unique<SendGain>(), {}, Kind::none);
                // In surround the fader's output is many channels: a post-fader send takes the
                // stereo sound before it and follows the fader's level instead.
                connectAudio (s.preFader || surroundMix ? preFader[t->id] : nodes.fader->nodeID, sendNode->nodeID);
                connectAudio (sendNode->nodeID, entry[s.target]);
            }
            for (auto it = nodes.sends.begin(); it != nodes.sends.end();)
            {
                if (keep.count (it->first)) { ++it; continue; }
                bury (it->second);
                it = nodes.sends.erase (it);
            }
        }
        // The Note Splitter's outputs (decision 0083): each a node reading what the
        // splitter sent it, into a track's MIDI input. Wired last, so a route that
        // would feed back into its own splitter is seen, and left out.
        std::set<std::pair<Id, int>> keepSplits;
        for (auto* t : all)
            for (auto& d : t->devices)
            {
                if (! d.active || d.midiTo.empty() || ! wantedDevices.count (d.id)) continue;
                auto* timed = dynamic_cast<TimedPlugin*> (deviceNodes[d.id]->getProcessor());
                auto* splitter = timed ? dynamic_cast<NoteSplitterProcessor*> (&timed->plugin()) : nullptr;
                if (! splitter) continue;
                for (int k = 0; k < (int) d.midiTo.size(); ++k)
                {
                    const auto target = tracks.find (d.midiTo[(size_t) k]);
                    if (d.midiTo[(size_t) k] == 0 || d.midiTo[(size_t) k] == t->id || target == tracks.end() || ! target->second.source) continue;
                    const auto to = target->second.source->nodeID, from = deviceNodes[d.id]->nodeID;
                    if (graph.isAnInputTo (to, from)) continue;
                    auto& node = splitNodes[{ d.id, k }];
                    if (! node) node = graph.addNode (std::make_unique<NoteSplitOutput>(), {}, Kind::none);
                    if (auto* o = dynamic_cast<NoteSplitOutput*> (node->getProcessor())) o->set (splitter, k);
                    connectMidi (from, node->nodeID);
                    connectMidi (node->nodeID, to);
                    keepSplits.insert ({ d.id, k });
                }
            }
        for (auto it = heldDelays.begin(); it != heldDelays.end();)
        {
            if (keepHeld.count (it->first)) { ++it; continue; }
            bury (it->second);
            it = heldDelays.erase (it);
        }
        for (auto it = splitNodes.begin(); it != splitNodes.end();)
        {
            if (keepSplits.count (it->first)) { ++it; continue; }
            bury (it->second);
            it = splitNodes.erase (it);
        }
        if (only == 0) connectAudio (metronomeNode->nodeID, audioOut->nodeID);
        graph.rebuild();
    }

    // Each Note Splitter knows its device and how many outputs it has (decision 0083).
    for (auto* t : all)
        for (auto& d : t->devices)
            if (d.active && wantedDevices.count (d.id))
                if (auto* timed = dynamic_cast<TimedPlugin*> (deviceNodes[d.id]->getProcessor()))
                    if (auto* splitter = dynamic_cast<NoteSplitterProcessor*> (&timed->plugin()))
                    {
                        splitter->outputCount = jmax (1, (int) d.midiTo.size());
                        splitter->deviceId = d.id;
                    }

    // The master reaches a Summing track's recorder this long after the song
    // position it belongs to: the graph's delay, less the Cue track's after it.
    {
        int afterMaster = 0;
        if (cue)
            for (auto& d : cue->devices)
                if (d.active && ! d.bypassed)
                    if (auto it = deviceNodes.find (d.id); it != deviceNodes.end())
                        afterMaster += it->second->getProcessor()->getLatencySamples();
        const double seconds = jmax (0, graph.getLatencySamples() - afterMaster) / rate;
        for (auto& [id, nodes] : tracks)
            if (nodes.recorder)
                if (auto* r = dynamic_cast<TrackSource*> (nodes.recorder->getProcessor())) r->recordLatencySeconds = seconds;
    }

    // ARA plug-ins (decision 0028): started once, then kept matching their track's audio clips.
    araTracks.clear();
    for (auto* t : all)
        for (auto& d : t->devices)
        {
            if (! d.active || ! d.useAra || ! wantedDevices.count (d.id)) continue;
            auto* inst = instance (d.id);
            if (! inst) continue;
            auto& session = araSessions[d.id];
            if (! session) { session = startAra (*inst, d.araState, [this] { if (onAudioReady) onAudioReady(); }); araOwner[d.id] = inst; }
            if (! session || ! session->ready()) continue;
            session->update (project, *t, folder, [this, inst] (std::function<void()> change)
            {
                graph.suspendProcessing (true);
                inst->releaseResources();
                change();
                inst->prepareToPlay (rate, blockSize);
                graph.suspendProcessing (false);
            });
            araTracks.insert (t->id);
        }
    for (auto it = araSessions.begin(); it != araSessions.end();)
        it = it->second ? std::next (it) : araSessions.erase (it);

    // What every track plays, and how its fader and sends are set.
    shared.anySolo = anySolo;
    for (auto* t : all)
    {
        auto& nodes = tracks[t->id];
        if (auto* f = dynamic_cast<Fader*> (nodes.fader->getProcessor()))
        {
            f->measureLoudness = t->kind == TrackKind::Master;
            f->volumeDb = (float) t->volumeDb;
            f->pan = (float) t->pan;
            f->audible = heard (*t) && t->enabled;
            f->phaseInvert = t->phaseInvert;
            f->mono = t->mono;
            f->format = (int) project.surround;
            f->tap = t->kind == TrackKind::Master || t->id == live;   // the analysers and the tuner (decision 0044)
            f->bus = t->kind == TrackKind::Master || t->isFolder() || t->isCue();
            f->frontBack = (float) t->frontBack;
        }
        for (auto& s : t->sends)
            if (auto it = nodes.sends.find (s.target); it != nodes.sends.end())
                if (auto* g = dynamic_cast<SendGain*> (it->second->getProcessor()))
                {
                    g->gainDb = (float) s.gainDb;
                    g->automatedDb = std::numeric_limits<float>::quiet_NaN();   // a lane sets it again below
                    g->follow = project.surround != SurroundFormat::Stereo && ! s.preFader
                                  ? dynamic_cast<Fader*> (nodes.fader->getProcessor()) : nullptr;
                    g->on = ! s.muted;
                }

        auto data = std::make_unique<TrackSource::Data>();
        data->compiled = playback::compile (project, *t);
        if (t->frozen && ! t->frozenFile.empty())
        {
            // A frozen track plays its render, from the song's start, instead of its clips.
            data->compiled.midi.clear();
            data->compiled.audio.clear();
            Clip frozen;
            frozen.id = t->id;
            frozen.kind = Clip::Kind::Audio;
            frozen.audio.file = t->frozenFile;
            double seconds = 0, sr = 0;
            int ch = 0;
            if (AudioCache::describe (File (t->frozenFile), seconds, sr, ch))
            {
                frozen.length = project.tempo.tickAt (seconds);
                playback::AudioRegion r;
                r.clip = t->id;
                r.startSeconds = 0;
                r.endSeconds = seconds;
                r.file = t->frozenFile;
                data->compiled.audio.push_back (r);
                data->regionAudio.push_back (cache.forClip (frozen, project.tempo, folder));
            }
        }
        if (araTracks.count (t->id)) data->compiled.audio.clear();   // the ARA plug-in plays them
        if (! t->frozen)
            for (auto& r : data->compiled.audio)
            {
                const Clip* clip = project.clip (r.clip);
                data->regionAudio.push_back (clip ? cache.forClip (*clip, project.tempo, folder) : nullptr);
            }
        data->tempo = project.tempo;
        const bool armedAudio = t->armedAudio();
        data->monitor = t->monitor == Monitor::In || (armedAudio && t->monitor == Monitor::Auto);
        data->armed = t->armed;
        // MIDI played in is kept when armed for MIDI of either kind (decisions 0064, 0065).
        data->armedMidi = t->armedPlugins() || t->armedMidiIn();
        data->summing = t->kind == TrackKind::Summing;
        data->readAutomation = t->autoMode != AutoMode::Off;
        data->fader = dynamic_cast<Fader*> (nodes.fader->getProcessor());
        for (auto& lane : t->automation)
        {
            switch (lane.target.kind)
            {
                case AutoTarget::Kind::Volume: data->hasVolume = lane.points.size() > 1; data->volume = lane; break;
                case AutoTarget::Kind::Pan:    data->hasPan = lane.points.size() > 1; data->pan = lane; break;
                case AutoTarget::Kind::DeviceParam:
                case AutoTarget::Kind::MorphX:
                case AutoTarget::Kind::MorphY:
                {
                    auto* inst = instance (lane.target.device);
                    if (! inst) break;
                    const int index = lane.target.kind == AutoTarget::Kind::MorphX ? 0
                                    : lane.target.kind == AutoTarget::Kind::MorphY ? 1 : lane.target.param;
                    if (auto* p = inst->getParameters()[index])
                        if (lane.points.size() > 1)
                        {
                            auto node = deviceNodes.find (lane.target.device);
                            auto* host = node != deviceNodes.end() && node->second ? dynamic_cast<TimedPlugin*> (node->second->getProcessor()) : nullptr;
                            data->params.push_back ({ lane, p, host });
                        }
                    break;
                }
                case AutoTarget::Kind::Mute: data->hasMute = ! lane.points.empty(); data->mute = lane; break;
                case AutoTarget::Kind::SendLevel:
                    if (auto it = nodes.sends.find (lane.target.send); it != nodes.sends.end() && lane.points.size() > 1)
                        data->sends.push_back ({ lane, dynamic_cast<SendGain*> (it->second->getProcessor()) });
                    break;
            }
        }
        for (auto& m : t->modulators)
        {
            TrackSource::ModulatorRun run;
            run.mod = m;
            for (auto& target : m.targets)
            {
                auto* inst = instance (target.device);
                auto* p = inst ? inst->getParameters()[target.param] : nullptr;
                run.params.push_back (p);
                // The base is where the parameter sits before any modulation;
                // a parameter already modulated keeps the base it had.
                float base = p ? p->getValue() : 0.5f;
                if (auto* old = dynamic_cast<TrackSource*> (nodes.source->getProcessor())->data.peek())
                    for (auto& om : old->modulators)
                        for (size_t k = 0; k < om.mod.targets.size() && k < om.bases.size(); ++k)
                            if (om.mod.targets[k].device == target.device && om.mod.targets[k].param == target.param)
                                base = om.bases[k];
                run.bases.push_back (base);
            }
            if (m.kind == ModKind::EnvelopeFollower && m.sourceTrack != 0)
                if (auto* f = fader (m.sourceTrack)) run.followLevel = &f->level;
            data->modulators.push_back (std::move (run));
        }
        if (auto* src = dynamic_cast<TrackSource*> (nodes.source->getProcessor()))
        {
            src->keepInput (armedAudio || t->monitor == Monitor::In);
            // Armed for MIDI (decision 0064): the tap goes on the first instrument (what
            // it's played, or what it sends), else the last MIDI effect.
            TimedPlugin* tapAt = nullptr;
            if (t->armedPlugins())
            {
                for (auto& d : t->devices)
                {
                    if (! d.active || d.role == DeviceRole::AudioFx) continue;
                    auto it = deviceNodes.find (d.id);
                    if (it == deviceNodes.end() || ! it->second) continue;
                    auto* tp = dynamic_cast<TimedPlugin*> (it->second->getProcessor());
                    if (! tp) continue;
                    tapAt = tp;
                    if (d.role == DeviceRole::Instrument) break;
                }
            }
            for (auto& d : t->devices)
                if (auto it = deviceNodes.find (d.id); it != deviceNodes.end() && it->second)
                    if (auto* tp = dynamic_cast<TimedPlugin*> (it->second->getProcessor()))
                        tp->tap = tp == tapAt ? static_cast<MidiTap*> (src) : nullptr;
            src->recordOutput = tapAt != nullptr;
            src->data.post (std::move (data));
        }
    }
}

//==============================================================================
static std::unique_ptr<AudioFormat> formatFor (const String& name)
{
    if (name == "aiff") return std::make_unique<AiffAudioFormat>();
    if (name == "flac") return std::make_unique<FlacAudioFormat>();
    return std::make_unique<WavAudioFormat>();
}

bool Engine::render (const Project& project, const File& folder, const File& out, const RenderOptions& o,
                     std::function<bool (float)> progress, String& error)
{
    const bool wasPlaying = playing.load();
    const Id wasLive = liveTrack;
    playing = false;
    deviceManager.removeAudioCallback (this);
    // Every channel of the mix's format is rendered (decision 0040).
    const int outChannels = surround::channels (project.surround);
    graph.setPlayConfigDetails (0, outChannels, rate, 512);
    graph.prepareToPlay (rate, 512);   // the graph's ends take on the channels before wiring
    rendering = true;   // the Cue track isn't in what's exported (decision 0051)
    sync (project, 0, folder);

    // Clips still loading are waited for (a render must not miss them).
    for (int tries = 0; tries < 600; ++tries)
    {
        bool waiting = false;
        for (auto& t : project.tracks)
            for (auto& c : t.clips())
                if (! c.isMidi() && ! cache.forClip (c, project.tempo, folder)) waiting = true;
        if (! waiting) break;
        Thread::sleep (50);
    }
    sync (project, 0, folder);

    const double renderRate = rate;
    const int block = 512;
    // Every fader and send starts at its level, not faded from where the last play left it.
    for (auto& [id, n] : tracks)
    {
        if (n.fader) if (auto* f = dynamic_cast<Fader*> (n.fader->getProcessor())) f->snap = true;
        for (auto& [to, s] : n.sends) if (auto* g = dynamic_cast<SendGain*> (s->getProcessor())) g->snap = true;
    }
    graph.setNonRealtime (true);
    graph.setPlayConfigDetails (0, outChannels, renderRate, block);
    graph.prepareToPlay (renderRate, block);

    out.deleteFile();
    const bool mp3 = o.format == "mp3";
    auto fmt = formatFor (o.format);
    std::unique_ptr<AudioFormatWriter> writer;
    if (! mp3)
    {
        // The song's details: RIFF INFO and ISRC in a WAV (decision 0041).
        std::unordered_map<String, String> tags;
        const auto& m = o.metadata;
        if (o.format == "wav")
        {
            auto put = [&tags] (const char* key, const std::string& v) { if (! v.empty()) tags[key] = String (v); };
            put (WavAudioFormat::riffInfoTitle, m.title);
            put (WavAudioFormat::riffInfoArtist, m.artist);
            put (WavAudioFormat::riffInfoProductName, m.album);
            put (WavAudioFormat::riffInfoGenre, m.genre);
            put (WavAudioFormat::riffInfoComment, m.comment);
            if (m.year > 0) put (WavAudioFormat::riffInfoDateCreated, std::to_string (m.year));
            if (m.trackNumber > 0) put (WavAudioFormat::riffInfoTrackNumber, std::to_string (m.trackNumber));
            put (WavAudioFormat::internationalStandardRecordingCode, m.isrc);
        }
        std::unique_ptr<OutputStream> stream (out.createOutputStream().release());
        if (stream)
            writer = fmt->createWriterFor (stream, AudioFormatWriterOptions().withSampleRate (renderRate)
                                                       .withNumChannels (outChannels).withBitsPerSample (o.bitDepth)
                                                       .withMetadataValues (tags));
    }
    if (! writer && ! mp3)
    {
        error = "The file couldn't be written.";
        graph.setNonRealtime (false);
        deviceManager.addAudioCallback (this);
        rendering = false;
        shape.clear();
        sync (project, wasLive, folder);
        return false;
    }

    // The whole render is kept so it can be normalised and dithered at the end.
    const double total = (o.end - o.start) + o.tail;
    const auto frames = (int) std::ceil (total * renderRate);
    // How late the output is: the plan's count (what can be heard), else JUCE's graph's.
    int latency = graph.outputLatency();
    if (latency < 0) latency = graph.getLatencySamples();
    AudioBuffer<float> result (outChannels, frames);
    result.clear();
    AudioBuffer<float> buf (outChannels, block);
    MidiBuffer midi;
    auto& c = shared.clock;
    double t = o.start;
    bool cancelled = false;
    for (int done = -latency; done < frames; done += block)
    {
        c.playing = true;
        c.recording = false;
        c.start = t;
        c.samples = block;
        c.rate = renderRate;
        c.wrapAt = -1;
        c.jumped = done == -latency;
        c.ppq = project.tempo.ticksAt (t) / PPQ;
        c.bpm = project.tempo.bpmAt ((Tick) (c.ppq * PPQ));
        info.setBpm (c.bpm);
        info.setPpqPosition (c.ppq);
        info.setIsPlaying (true);
        info.setTimeInSeconds (t);
        buf.clear();
        midi.clear();
        graph.processBlock (buf, midi);
        for (int i = 0; i < block; ++i)
        {
            const int at = done + i;
            if (at >= 0 && at < frames)
                for (int ch = 0; ch < outChannels; ++ch) result.setSample (ch, at, buf.getSample (ch, i));
        }
        t += block / renderRate;
        if (progress && ! progress ((float) jlimit (0.0, 1.0, (double) (done + latency) / (frames + latency))))
        {
            cancelled = true;
            break;
        }
    }

    if (! cancelled)
    {
        if (o.loudnessTarget < 0)
        {
            // To a loudness, but never past the true-peak ceiling.
            const auto found = loudness::measure (result.getReadPointer (0), result.getReadPointer (1), frames, renderRate);
            if (found.integrated > loudness::silence + 1)
            {
                double gainDb = o.loudnessTarget - found.integrated;
                gainDb = std::min (gainDb, o.truePeakCeiling - found.truePeak);
                result.applyGain (Decibels::decibelsToGain ((float) gainDb));
                lastExportLoudness = found.integrated + gainDb;
            }
        }
        else if (o.normalise)
        {
            const float peak = result.getMagnitude (0, frames);
            if (peak > 0) result.applyGain (Decibels::decibelsToGain (-0.3f) / peak);
        }
        if (o.dither && (o.bitDepth < 32 || mp3))
        {
            // Triangular dither at the last bit.
            Random r;
            const float lsb = 1.0f / (float) (1 << ((mp3 ? 16 : o.bitDepth) - 1));
            for (int ch = 0; ch < outChannels; ++ch)
                for (int i = 0; i < frames; ++i)
                    result.addSample (ch, i, (r.nextFloat() - r.nextFloat()) * lsb);
        }
        if (mp3)
        {
            if (! writeMp3 (out, result, renderRate, o.metadata, o.mp3Kbps, error)) cancelled = true;
        }
        else if (writer->isFloatingPoint()) writer->writeFromAudioSampleBuffer (result, 0, frames);
        else
        {
            // Whole words, each the nearest step of the file's word length (the audio
            // engine's document: the least distortion a shorter word allows; JUCE's own
            // conversion truncates, a step off for half the samples). A word as long
            // as the source's, or longer, holds it exactly.
            const int bits = jlimit (8, 32, writer->getBitsPerSample());
            const double steps = std::ldexp (1.0, bits - 1);
            const double low = -steps, high = steps - 1;
            std::vector<std::vector<int>> words ((size_t) outChannels, std::vector<int> ((size_t) jmin (frames, 65536)));
            std::vector<const int*> chans ((size_t) outChannels + 1, nullptr);
            for (int from = 0; from < frames; from += 65536)
            {
                const int count = jmin (65536, frames - from);
                for (int ch = 0; ch < outChannels; ++ch)
                {
                    const float* x = result.getReadPointer (ch, from);
                    auto& w = words[(size_t) ch];
                    for (int i = 0; i < count; ++i)
                    {
                        const double q = jlimit (low, high, std::round ((double) x[i] * steps));
                        w[(size_t) i] = (int) ((long long) q * (1LL << (32 - bits)));
                    }
                    chans[(size_t) ch] = w.data();
                }
                writer->write (chans.data(), count);
            }
        }
    }
    writer.reset();
    if (cancelled) out.deleteFile();

    graph.setNonRealtime (false);
    c.playing = false;
    playing = wasPlaying;
    deviceManager.addAudioCallback (this);
    // Wired again for the interface's channels.
    rendering = false;
    shape.clear();
    sync (project, wasLive, folder);
    return ! cancelled;
}

bool Engine::renderTrack (const Project& project, Id track, const File& folder, const File& out,
                          double start, double end, double tail, String& error)
{
    renderOnlyTrack = track;
    RenderOptions o;
    o.start = start;
    o.end = end;
    o.tail = tail;
    o.bitDepth = 32;           // 32-bit float WAV (decision 0079)
    o.dither = false;
    o.format = "wav";
    const bool ok = render (project, folder, out, o, {}, error);
    renderOnlyTrack = 0;
    shape.clear();
    sync (project, liveTrack, folder);
    return ok;
}

bool Engine::renderClips (const Project& project, Id trackId, const std::set<Id>& chosen, Tick from, Tick to,
                          const File& folder, const File& out, double& peak, String& error)
{
    const auto* original = project.track (trackId);
    if (! original) { error = "The track isn't there."; return false; }
    Track t = *original;
    t.delayMs = 0;
    std::erase_if (t.clips(), [&] (const Clip& c) { return ! chosen.count (c.id) || c.isMidi(); });
    for (auto& c : t.clips()) c.muted = false;
    const auto compiled = playback::compile (project, t);
    const double start = project.tempo.secondsAt (from), end = project.tempo.secondsAt (to);
    const auto first = (long long) std::llround (start * rate);
    const int frames = (int) jmax<long long> (1, (long long) std::llround (end * rate) - first);
    std::vector<double> l ((size_t) frames, 0.0), r ((size_t) frames, 0.0);
    for (auto& region : compiled.audio)
    {
        const Clip* clip = nullptr;
        for (auto& c : t.clips()) if (c.id == region.clip) clip = &c;
        if (! clip) continue;
        const auto& a = clip->audio;
        const auto& name = a.renderedFile.empty() ? a.file : a.renderedFile;
        const File f = File::isAbsolutePath (name) ? File (name) : folder.getChildFile (name);
        auto plain = cache.file (f);
        if (! plain) { error = "The audio of " + String (clip->name) + " couldn't be read."; return false; }
        const bool warped = a.warp != WarpMode::Off || a.reversed || std::abs (a.transpose) > 1e-6;
        const auto played = warped ? std::shared_ptr<const CachedAudio> (AudioCache::render (*plain, *clip, project.tempo, rate)) : plain;
        mixRegion (region, played->buffer, rate, first, frames, l.data(), r.data());
    }
    peak = 0;
    for (int i = 0; i < frames; ++i) peak = std::max ({ peak, std::abs (l[(size_t) i]), std::abs (r[(size_t) i]) });
    const double scale = peak > 0 ? 1.0 / peak : 1.0;
    AudioBuffer<float> b (2, frames);
    for (int i = 0; i < frames; ++i)
    {
        b.setSample (0, i, (float) (l[(size_t) i] * scale));
        b.setSample (1, i, (float) (r[(size_t) i] * scale));
    }
    out.deleteFile();
    auto w = audiofile::floatWav (out, rate, 2);
    if (! w || ! w->writeFromAudioSampleBuffer (b, 0, frames)) { error = "The file couldn't be written."; return false; }
    if (peak <= 0) peak = 1;
    return true;
}

bool Engine::renderOfflineFx (const Clip& clip, const File& source, const File& out, String& error)
{
    auto audio = cache.file (source);
    if (! audio) { error = "The clip's audio couldn't be read."; return false; }
    const int block = 512;
    std::vector<std::unique_ptr<AudioPluginInstance>> chain;
    for (auto& d : clip.offlineFx.chain)
    {
        if (! d.active) continue;
        auto inst = pluginManager.create (d.plugin, rate, block, error);
        if (! inst) return false;
        if (! d.state.empty())
        {
            MemoryBlock m;
            if (m.fromBase64Encoding (d.state)) inst->setStateInformation (m.getData(), (int) m.getSize());
        }
        inst->setNonRealtime (true);
        inst->setPlayConfigDetails (2, 2, rate, block);
        inst->prepareToPlay (rate, block);
        chain.push_back (std::move (inst));
    }
    int latency = 0;
    double tail = 0;
    for (auto& p : chain) { latency += p->getLatencySamples(); tail = jmax (tail, p->getTailLengthSeconds()); }
    const int frames = audio->buffer.getNumSamples();
    const int extra = clip.offlineFx.includeTail ? (int) (jmin (tail, 10.0) * rate) : 0;
    AudioBuffer<float> result (2, frames + extra);
    result.clear();
    AudioBuffer<float> buf (2, block);
    MidiBuffer midi;
    for (int done = 0; done < frames + extra + latency; done += block)
    {
        buf.clear();
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
                if (done + i < frames) buf.setSample (ch, i, audio->buffer.getSample (ch, done + i));
        for (auto& p : chain) { midi.clear(); p->processBlock (buf, midi); }
        for (int i = 0; i < block; ++i)
        {
            const int at = done + i - latency;
            if (at >= 0 && at < result.getNumSamples())
                for (int ch = 0; ch < 2; ++ch) result.setSample (ch, at, buf.getSample (ch, i));
        }
    }
    auto writer = audiofile::floatWav (out, rate, 2);   // decision 0079
    if (! writer) { error = "The file couldn't be written."; return false; }
    writer->writeFromAudioSampleBuffer (result, 0, result.getNumSamples());
    return true;
}

} // namespace kal
