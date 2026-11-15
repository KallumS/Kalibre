#include "Engine.h"

#include "Edit.h"

namespace kal
{
using namespace juce;
using IOProc = AudioProcessorGraph::AudioGraphIOProcessor;

Engine::Engine (PluginManager& pm) : pluginManager (pm)
{
    graph.setPlayHead (this);
    tempo.post (std::make_unique<TempoMap>());
    // The graph's own ends exist from the start, so a project can be synced
    // (and rendered) with no audio device at all. Its channels are set first:
    // a connection to a channel the end doesn't have yet is refused.
    graph.setPlayConfigDetails (2, 2, rate, blockSize);
    audioIn = graph.addNode (std::make_unique<IOProc> (IOProc::audioInputNode));
    audioOut = graph.addNode (std::make_unique<IOProc> (IOProc::audioOutputNode));
    midiIn = graph.addNode (std::make_unique<IOProc> (IOProc::midiInputNode));
    metronomeNode = graph.addNode (std::make_unique<Metronome> (shared));
    startTimerHz (20);
}

Engine::~Engine()
{
    stopTimer();
    deviceManager.removeAudioCallback (this);
    deviceManager.removeMidiInputDeviceCallback ({}, this);
    deviceManager.removeMidiInputDeviceCallback ({}, &player);
    player.setProcessor (nullptr);
    for (auto& [id, node] : deviceNodes)
        if (node) node->getProcessor()->removeListener (this);
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
    deviceManager.addMidiInputDeviceCallback ({}, this);
    deviceManager.addMidiInputDeviceCallback ({}, &player);

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
    shared.clock.rate = rate;
    cache.setSampleRate (rate);
    player.audioDeviceAboutToStart (device);
}

void Engine::audioDeviceStopped()
{
    player.audioDeviceStopped();
}

void Engine::audioDeviceIOCallbackWithContext (const float* const* ins, int numIns, float* const* outs, int numOuts,
                                               int n, const AudioIODeviceCallbackContext& context)
{
    auto& c = shared.clock;
    const auto* map = tempo.take();
    double p = pos.load();
    c.jumped = false;
    if (const double s = seekTo.exchange (-1.0); s > -1.0e8)
    {
        p = s;
        c.jumped = true;
    }
    c.playing = playing.load();
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
    player.audioDeviceIOCallbackWithContext (ins, numIns, outs, numOuts, n, context);
    if (c.playing) pos.store (next);
    else pos.store (p);
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
void Engine::handleIncomingMidiMessage (MidiInput*, const MidiMessage& m)
{
    if (m.getRawDataSize() > 3 || m.isActiveSense() || m.isMidiClock()) return;
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
        if (node && node->getProcessor() == p) return id;
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

void Engine::audioProcessorChanged (AudioProcessor*, const ChangeDetails& d)
{
    if (d.latencyChanged)
        MessageManager::callAsync ([this] { graph.rebuild(); });
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
    const auto now = Time::getMillisecondCounter();
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [now] (const auto& g) { return now - g.second > 1500; }),
                     graveyard.end());
    for (auto& [id, t] : tracks)
        if (auto* s = dynamic_cast<TrackSource*> (t.source ? t.source->getProcessor() : nullptr)) s->data.collect();
    tempo.collect();
}

//==============================================================================
AudioPluginInstance* Engine::instance (Id device) const
{
    auto it = deviceNodes.find (device);
    return it == deviceNodes.end() || ! it->second ? nullptr : dynamic_cast<AudioPluginInstance*> (it->second->getProcessor());
}

Fader* Engine::fader (Id track) const
{
    auto it = tracks.find (track);
    return it == tracks.end() || ! it->second.fader ? nullptr : dynamic_cast<Fader*> (it->second.fader->getProcessor());
}

TrackSource* Engine::source (Id track) const
{
    auto it = tracks.find (track);
    return it == tracks.end() || ! it->second.source ? nullptr : dynamic_cast<TrackSource*> (it->second.source->getProcessor());
}

std::string Engine::stateOf (Id device) const
{
    auto* p = instance (device);
    if (! p) return {};
    MemoryBlock m;
    p->getStateInformation (m);
    return m.toBase64Encoding().toStdString();
}

Engine::Node::Ptr Engine::nodeForDevice (const Device& d, String& error)
{
    const auto key = d.plugin.format + "|" + d.plugin.identifier + "|" + std::to_string (d.plugin.uid);
    if (auto it = deviceNodes.find (d.id); it != deviceNodes.end() && deviceKeys[d.id] == key)
        return it->second;
    auto instance = pluginManager.create (d.plugin, rate, blockSize, error);
    if (! instance) return nullptr;
    if (! d.state.empty())
    {
        MemoryBlock m;
        if (m.fromBase64Encoding (d.state)) instance->setStateInformation (m.getData(), (int) m.getSize());
    }
    instance->addListener (this);
    auto node = graph.addNode (std::move (instance));
    if (auto old = deviceNodes[d.id]) { old->getProcessor()->removeListener (this); bury (old); }
    deviceNodes[d.id] = node;
    deviceKeys[d.id] = key;
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

    // Nodes for every track, and for every active device.
    std::set<Id> wantedTracks, wantedDevices;
    for (auto* t : all)
    {
        wantedTracks.insert (t->id);
        auto& nodes = tracks[t->id];
        if (! nodes.source) nodes.source = graph.addNode (std::make_unique<TrackSource> (shared, t->id), {}, Kind::none);
        if (! nodes.fader) nodes.fader = graph.addNode (std::make_unique<Fader>(), {}, Kind::none);
        for (auto& d : t->devices)
        {
            if (! d.active) continue;
            String error;
            if (nodeForDevice (d, error)) wantedDevices.insert (d.id);
            else if (onMessage) onMessage (String (d.plugin.name) + " couldn't be loaded. " + error);
        }
    }
    for (auto it = tracks.begin(); it != tracks.end();)
    {
        if (wantedTracks.count (it->first)) { ++it; continue; }
        bury (it->second.source);
        bury (it->second.fader);
        for (auto& s : it->second.sends) bury (s.second);
        it = tracks.erase (it);
    }
    for (auto it = deviceNodes.begin(); it != deviceNodes.end();)
    {
        if (wantedDevices.count (it->first)) { ++it; continue; }
        if (it->second) it->second->getProcessor()->removeListener (this);
        bury (it->second);
        deviceKeys.erase (it->first);
        it = deviceNodes.erase (it);
    }

    // Solo: a track is heard when nothing is soloed, or it is, or a folder
    // around it is, or something inside it is. Returns are always heard.
    std::set<Id> soloed;
    for (auto* t : all) if (t->solo) soloed.insert (t->id);
    const bool anySolo = ! soloed.empty();
    auto heard = [&] (const Track& t)
    {
        if (t.mute) return false;
        if (! anySolo || t.kind == TrackKind::Master || t.kind == TrackKind::Return) return true;
        if (soloed.count (t.id)) return true;
        for (auto id : soloed) if (project.isInside (t.id, id) || project.isInside (id, t.id)) return true;
        // Something soloed sends to... no: only routing up the tree counts.
        return false;
    };

    // The wiring, as text: rebuilt only when it changes.
    String newShape;
    for (auto* t : all)
    {
        newShape << (int64) t->id << ":" << (int) t->kind << ":" << (int64) project.outputOf (t->id) << ":" << (t->id == live ? 1 : 0)
                 << (t->armed ? "a" : "") << (int) t->monitor << "/" << t->input.audioChannel << "[";
        for (auto& d : t->devices) if (d.active && wantedDevices.count (d.id)) newShape << (int64) d.id << (int) d.role << (d.bypassed ? "b" : "") << ",";
        newShape << "]{";
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
        auto connectMidi = [&] (NodeID from, NodeID to)
        {
            graph.addConnection ({ { from, AudioProcessorGraph::midiChannelIndex }, { to, AudioProcessorGraph::midiChannelIndex } }, Kind::none);
        };

        // Where sound going into a track goes: its first audio effect, or its fader.
        std::map<Id, NodeID> entry;
        std::map<Id, NodeID> preFader;
        for (auto* t : all)
        {
            auto& nodes = tracks[t->id];
            std::vector<NodeID> fx;
            for (auto& d : t->devices)
                if (d.active && d.role == DeviceRole::AudioFx && wantedDevices.count (d.id)) fx.push_back (deviceNodes[d.id]->nodeID);
            entry[t->id] = fx.empty() ? nodes.fader->nodeID : fx.front();
            for (size_t i = 0; i + 1 < fx.size(); ++i) connectAudio (fx[i], fx[i + 1]);
            if (! fx.empty()) connectAudio (fx.back(), nodes.fader->nodeID);
            preFader[t->id] = fx.empty() ? nodes.source->nodeID : fx.back();
        }

        const int inputs = deviceManager.getCurrentAudioDevice()
                             ? deviceManager.getCurrentAudioDevice()->getActiveInputChannels().countNumberOfSetBits() : 0;
        for (auto* t : all)
        {
            auto& nodes = tracks[t->id];
            const auto src = nodes.source->nodeID;
            // Live input.
            const bool monitorIn = t->monitor == Monitor::In || (t->armed && t->monitor == Monitor::Auto);
            if ((t->armed || monitorIn) && t->input.audioChannel >= 0 && inputs > 0)
                for (int ch = 0; ch < 2; ++ch)
                {
                    const int inCh = jmin (inputs - 1, t->input.audioChannel + (t->input.stereo ? ch : 0));
                    graph.addConnection ({ { audioIn->nodeID, inCh }, { src, ch } }, Kind::none);
                }
            if (t->id == live || t->armed) connectMidi (midiIn->nodeID, src);

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

            // Out: to its folder, the track it is routed to, or the master.
            if (t->kind == TrackKind::Master)
                connectAudio (nodes.fader->nodeID, audioOut->nodeID);
            else
            {
                const Id out = project.outputOf (t->id);
                connectAudio (nodes.fader->nodeID, entry[out == 0 ? project.master.id : out]);
            }

            // Sends.
            std::set<Id> keep;
            for (auto& s : t->sends)
            {
                if (! project.track (s.target) || ! edit::canRoute (project, t->id, s.target)) continue;
                keep.insert (s.target);
                auto& sendNode = nodes.sends[s.target];
                if (! sendNode) sendNode = graph.addNode (std::make_unique<SendGain>(), {}, Kind::none);
                connectAudio (s.preFader ? preFader[t->id] : nodes.fader->nodeID, sendNode->nodeID);
                connectAudio (sendNode->nodeID, entry[s.target]);
            }
            for (auto it = nodes.sends.begin(); it != nodes.sends.end();)
            {
                if (keep.count (it->first)) { ++it; continue; }
                bury (it->second);
                it = nodes.sends.erase (it);
            }
        }
        connectAudio (metronomeNode->nodeID, audioOut->nodeID);
        graph.rebuild();
    }

    // What every track plays, and how its fader and sends are set.
    shared.anySolo = anySolo;
    for (auto* t : all)
    {
        auto& nodes = tracks[t->id];
        if (auto* f = dynamic_cast<Fader*> (nodes.fader->getProcessor()))
        {
            f->volumeDb = (float) t->volumeDb;
            f->pan = (float) t->pan;
            f->audible = heard (*t) && t->enabled;
            f->phaseInvert = t->phaseInvert;
            f->mono = t->mono;
        }
        for (auto& s : t->sends)
            if (auto it = nodes.sends.find (s.target); it != nodes.sends.end())
                if (auto* g = dynamic_cast<SendGain*> (it->second->getProcessor()))
                {
                    g->gainDb = (float) s.gainDb;
                    g->on = ! s.muted;
                }

        auto data = std::make_unique<TrackSource::Data>();
        data->compiled = playback::compile (project, *t);
        for (auto& r : data->compiled.audio)
        {
            const Clip* clip = project.clip (r.clip);
            data->regionAudio.push_back (clip ? cache.forClip (*clip, project.tempo, folder) : nullptr);
        }
        data->tempo = project.tempo;
        data->monitor = t->monitor == Monitor::In || (t->armed && t->monitor == Monitor::Auto);
        data->armed = t->armed;
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
                        if (lane.points.size() > 1) data->params.push_back ({ lane, p });
                    break;
                }
                case AutoTarget::Kind::Mute:
                case AutoTarget::Kind::SendLevel:
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
            src->keepInput (t->armed || t->monitor == Monitor::In);
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
    playing = false;
    deviceManager.removeAudioCallback (this);
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
    graph.setNonRealtime (true);
    graph.setPlayConfigDetails (0, 2, renderRate, block);
    graph.prepareToPlay (renderRate, block);

    out.deleteFile();
    auto fmt = formatFor (o.format);
    std::unique_ptr<OutputStream> stream (out.createOutputStream().release());
    std::unique_ptr<AudioFormatWriter> writer;
    if (stream)
        writer = fmt->createWriterFor (stream, AudioFormatWriterOptions().withSampleRate (renderRate)
                                                   .withNumChannels (2).withBitsPerSample (o.bitDepth));
    if (! writer)
    {
        error = "The file couldn't be written.";
        graph.setNonRealtime (false);
        deviceManager.addAudioCallback (this);
        return false;
    }

    // The whole render is kept so it can be normalised and dithered at the end.
    const double total = (o.end - o.start) + o.tail;
    const auto frames = (int) std::ceil (total * renderRate);
    const int latency = graph.getLatencySamples();
    AudioBuffer<float> result (2, frames);
    result.clear();
    AudioBuffer<float> buf (2, block);
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
                for (int ch = 0; ch < 2; ++ch) result.setSample (ch, at, buf.getSample (ch, i));
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
        if (o.normalise)
        {
            const float peak = result.getMagnitude (0, frames);
            if (peak > 0) result.applyGain (Decibels::decibelsToGain (-0.3f) / peak);
        }
        if (o.dither && o.bitDepth < 32)
        {
            // Triangular dither at the last bit.
            Random r;
            const float lsb = 1.0f / (float) (1 << (o.bitDepth - 1));
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < frames; ++i)
                    result.addSample (ch, i, (r.nextFloat() - r.nextFloat()) * lsb);
        }
        writer->writeFromAudioSampleBuffer (result, 0, frames);
    }
    writer.reset();
    if (cancelled) out.deleteFile();

    graph.setNonRealtime (false);
    c.playing = false;
    playing = wasPlaying;
    deviceManager.addAudioCallback (this);
    return ! cancelled;
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
    out.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> stream (out.createOutputStream().release());
    auto writer = stream ? wav.createWriterFor (stream, AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (32))
                         : nullptr;
    if (! writer) { error = "The file couldn't be written."; return false; }
    writer->writeFromAudioSampleBuffer (result, 0, result.getNumSamples());
    return true;
}

} // namespace kal
