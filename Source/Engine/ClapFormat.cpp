#include "ClapFormat.h"

#include <clap/clap.h>

#include <atomic>
#include <map>

#if JUCE_LINUX || JUCE_BSD
 #include <juce_gui_extra/juce_gui_extra.h>
#endif

namespace kal::clap
{
using namespace juce;

namespace
{
thread_local bool onAudioThread = false;

//==============================================================================
// One loaded .clap: its entry is initialised once however many plug-ins use it.
struct Library
{
    explicit Library (const File& bundle)
    {
        File binary = bundle;
       #if JUCE_MAC
        if (bundle.isDirectory())
        {
            auto macos = bundle.getChildFile ("Contents/MacOS");
            auto files = macos.findChildFiles (File::findFiles, false);
            if (! files.isEmpty()) binary = files.getFirst();
        }
       #endif
        if (! lib.open (binary.getFullPathName())) return;
        entry = static_cast<const clap_plugin_entry_t*> (lib.getFunction ("clap_entry"));
        if (entry && clap_version_is_compatible (entry->clap_version) && entry->init (bundle.getFullPathName().toRawUTF8()))
            initialised = true;
        else
            entry = nullptr;
    }

    ~Library()
    {
        if (entry && initialised) entry->deinit();
        lib.close();
    }

    const clap_plugin_factory_t* factory() const
    {
        return entry ? static_cast<const clap_plugin_factory_t*> (entry->get_factory (CLAP_PLUGIN_FACTORY_ID)) : nullptr;
    }

    DynamicLibrary lib;
    const clap_plugin_entry_t* entry = nullptr;
    bool initialised = false;
};

std::shared_ptr<Library> openLibrary (const File& f)
{
    static std::map<String, std::weak_ptr<Library>> open;
    static CriticalSection lock;
    const ScopedLock sl (lock);
    auto& slot = open[f.getFullPathName()];
    if (auto existing = slot.lock()) return existing;
    auto made = std::make_shared<Library> (f);
    if (! made->entry) return nullptr;
    slot = made;
    return made;
}

int idHash (const char* clapId) { return String (clapId).hashCode(); }

bool hasFeature (const clap_plugin_descriptor_t* d, const char* feature)
{
    if (! d->features) return false;
    for (auto f = d->features; *f; ++f)
        if (std::strcmp (*f, feature) == 0) return true;
    return false;
}

void fillDescription (PluginDescription& p, const clap_plugin_descriptor_t* d, const File& file)
{
    p.name = String::fromUTF8 (d->name);
    p.descriptiveName = String::fromUTF8 (d->description ? d->description : d->name);
    p.manufacturerName = String::fromUTF8 (d->vendor ? d->vendor : "");
    p.version = String::fromUTF8 (d->version ? d->version : "");
    p.pluginFormatName = ClapPluginFormat::formatName();
    p.fileOrIdentifier = file.getFullPathName();
    p.lastFileModTime = file.getLastModificationTime();
    p.lastInfoUpdateTime = Time::getCurrentTime();
    p.uniqueId = p.deprecatedUid = idHash (d->id);
    p.isInstrument = hasFeature (d, CLAP_PLUGIN_FEATURE_INSTRUMENT);
    p.category = p.isInstrument ? "Instrument"
               : hasFeature (d, CLAP_PLUGIN_FEATURE_NOTE_EFFECT) ? "Note Effect"
               : hasFeature (d, CLAP_PLUGIN_FEATURE_ANALYZER) ? "Analyzer"
                                                              : "Effect";
    p.numInputChannels = p.isInstrument ? 0 : 2;
    p.numOutputChannels = hasFeature (d, CLAP_PLUGIN_FEATURE_NOTE_EFFECT) ? 0 : 2;
    p.hasSharedContainer = false;
    p.hasARAExtension = false;
}

//==============================================================================
// Events going into and out of a block, in a buffer reserved once.
struct EventList
{
    EventList() { bytes.resize (1 << 16); offsets.reserve (2048); }

    void clear() { used = 0; offsets.clear(); }

    bool push (const clap_event_header_t* h)
    {
        if (used + h->size > bytes.size() || offsets.size() == offsets.capacity()) return false;
        std::memcpy (bytes.data() + used, h, h->size);
        offsets.push_back (used);
        used += h->size;
        return true;
    }

    uint32_t size() const { return (uint32_t) offsets.size(); }
    const clap_event_header_t* get (uint32_t i) const
    {
        return reinterpret_cast<const clap_event_header_t*> (bytes.data() + offsets[i]);
    }

    clap_input_events_t input()
    {
        return { this,
                 [] (const clap_input_events_t* l) { return static_cast<EventList*> (l->ctx)->size(); },
                 [] (const clap_input_events_t* l, uint32_t i) { return static_cast<EventList*> (l->ctx)->get (i); } };
    }

    clap_output_events_t output()
    {
        return { this, [] (const clap_output_events_t* l, const clap_event_header_t* h)
                 { return static_cast<EventList*> (l->ctx)->push (h); } };
    }

    // Keeps the order stable by time, which CLAP requires.
    void sortByTime()
    {
        std::stable_sort (offsets.begin(), offsets.end(), [this] (size_t a, size_t b)
        {
            return reinterpret_cast<const clap_event_header_t*> (bytes.data() + a)->time
                 < reinterpret_cast<const clap_event_header_t*> (bytes.data() + b)->time;
        });
    }

    std::vector<uint8_t> bytes;
    std::vector<size_t> offsets;
    size_t used = 0;
};

class ClapInstance;

//==============================================================================
class ClapParameter : public HostedAudioProcessorParameter
{
public:
    ClapParameter (ClapInstance& o, const clap_param_info_t& info, double value);

    float getValue() const override { return normalised.load(); }
    void setValue (float v) override;
    float getDefaultValue() const override { return toNormal (defaultValue); }
    String getName (int maximumStringLength) const override { return name.substring (0, maximumStringLength); }
    String getLabel() const override { return {}; }
    String getText (float v, int) const override;
    float getValueForText (const String&) const override;
    bool isAutomatable() const override { return (flags & CLAP_PARAM_IS_AUTOMATABLE) != 0; }
    bool isDiscrete() const override { return (flags & CLAP_PARAM_IS_STEPPED) != 0; }
    bool isBoolean() const override { return isDiscrete() && max - min == 1.0; }
    int getNumSteps() const override
    {
        return isDiscrete() ? jmax (2, (int) (max - min) + 1) : AudioProcessor::getDefaultNumParameterSteps();
    }
    String getParameterID() const override { return String ((int64) id); }

    float toNormal (double plain) const { return max > min ? (float) jlimit (0.0, 1.0, (plain - min) / (max - min)) : 0.0f; }
    double toPlain (float n) const { return min + (double) n * (max - min); }

    ClapInstance& owner;
    clap_id id;
    void* cookie;
    String name;
    double min, max, defaultValue;
    uint32_t flags;
    std::atomic<float> normalised { 0.0f };
    std::atomic<bool> changedByPlugin { false };
    std::atomic<int> gesture { 0 };   // 1 began, 2 ended, from the plug-in
};

//==============================================================================
class ClapInstance : public AudioPluginInstance, private Timer
{
public:
    ClapInstance (std::shared_ptr<Library> library, const clap_plugin_descriptor_t* d, const PluginDescription& desc)
        : AudioPluginInstance (busesFor (desc)), lib (std::move (library)), description (desc)
    {
        host.clap_version = CLAP_VERSION;
        host.host_data = this;
        host.name = "Kalibre";
        host.vendor = "Kallum Shah";
        host.url = "https://github.com/KallumS/Kalibre";
        host.version = KALIBRE_VERSION;
        host.get_extension = [] (const clap_host_t* h, const char* id) { return self (h)->hostExtension (id); };
        host.request_restart = [] (const clap_host_t* h) { self (h)->requestRestart(); };
        host.request_process = [] (const clap_host_t*) {};
        host.request_callback = [] (const clap_host_t* h)
        {
            auto* s = self (h);
            MessageManager::callAsync ([w = WeakReference<ClapInstance> (s)]
            {
                if (auto* i = w.get()) i->plugin->on_main_thread (i->plugin);
            });
        };

        if (auto* f = lib->factory())
            plugin = f->create_plugin (f, &host, d->id);
        if (plugin && ! plugin->init (plugin))
        {
            plugin->destroy (plugin);
            plugin = nullptr;
        }
        if (! plugin) return;

        params   = static_cast<const clap_plugin_params_t*>   (plugin->get_extension (plugin, CLAP_EXT_PARAMS));
        state    = static_cast<const clap_plugin_state_t*>    (plugin->get_extension (plugin, CLAP_EXT_STATE));
        latency  = static_cast<const clap_plugin_latency_t*>  (plugin->get_extension (plugin, CLAP_EXT_LATENCY));
        gui      = static_cast<const clap_plugin_gui_t*>      (plugin->get_extension (plugin, CLAP_EXT_GUI));
        tail     = static_cast<const clap_plugin_tail_t*>     (plugin->get_extension (plugin, CLAP_EXT_TAIL));
        timers   = static_cast<const clap_plugin_timer_support_t*> (plugin->get_extension (plugin, CLAP_EXT_TIMER_SUPPORT));
        notePorts = static_cast<const clap_plugin_note_ports_t*> (plugin->get_extension (plugin, CLAP_EXT_NOTE_PORTS));

        if (notePorts && notePorts->count (plugin, true) > 0)
        {
            clap_note_port_info_t info {};
            if (notePorts->get (plugin, 0, true, &info))
                prefersClapNotes = (info.preferred_dialect == CLAP_NOTE_DIALECT_CLAP);
        }

        buildParameters();
        startTimerHz (30);
    }

    ~ClapInstance() override
    {
        stopTimer();
        editorClosing();
        if (plugin)
        {
            if (active) { stopProcessing(); plugin->deactivate (plugin); }
            plugin->destroy (plugin);
        }
        for (auto& t : timerObjects) t.second->stopTimer();
        masterReference.clear();
    }

    bool valid() const { return plugin != nullptr; }

    static BusesProperties busesFor (const PluginDescription& d)
    {
        BusesProperties b;
        if (d.numInputChannels > 0) b = b.withInput ("Input", AudioChannelSet::stereo(), true);
        if (d.numOutputChannels > 0) b = b.withOutput ("Output", AudioChannelSet::stereo(), true);
        return b;
    }

    static ClapInstance* self (const clap_host_t* h) { return static_cast<ClapInstance*> (h->host_data); }

    //==========================================================================
    const String getName() const override { return description.name; }
    void fillInPluginDescription (PluginDescription& d) const override { d = description; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return description.category == "Note Effect"; }
    bool isMidiEffect() const override { return description.category == "Note Effect"; }
    double getTailLengthSeconds() const override
    {
        return (tail && plugin && active) ? (double) tail->get (plugin) / jmax (1.0, getSampleRate()) : 0.0;
    }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}
    bool hasEditor() const override
    {
        if (! gui || ! plugin) return false;
       #if JUCE_MAC
        return gui->is_api_supported (plugin, CLAP_WINDOW_API_COCOA, false);
       #elif JUCE_LINUX || JUCE_BSD
        return gui->is_api_supported (plugin, CLAP_WINDOW_API_X11, false);
       #else
        return false;
       #endif
    }
    AudioProcessorEditor* createEditor() override;

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        for (auto& set : l.inputBuses)  if (! set.isDisabled() && set != AudioChannelSet::stereo()) return false;
        for (auto& set : l.outputBuses) if (! set.isDisabled() && set != AudioChannelSet::stereo()) return false;
        return true;
    }

    //==========================================================================
    void prepareToPlay (double sampleRate, int blockSize) override
    {
        if (! plugin) return;
        if (active) { stopProcessing(); plugin->deactivate (plugin); active = false; }
        lastRate = sampleRate;
        lastBlock = blockSize;
        active = plugin->activate (plugin, sampleRate, 1, (uint32_t) jmax (1, blockSize));
        scratchIn.setSize (2, jmax (1, blockSize));
        scratchOut.setSize (2, jmax (1, blockSize));
        if (latency && active) setLatencySamples ((int) latency->get (plugin));
    }

    void releaseResources() override
    {
        if (plugin && active)
        {
            stopProcessing();
            plugin->deactivate (plugin);
            active = false;
        }
    }

    void stopProcessing()
    {
        if (processing.exchange (false)) plugin->stop_processing (plugin);
    }

    void queueParameter (clap_id id, double plain)
    {
        int s1, n1, s2, n2;
        paramFifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 > 0) paramQueue[(size_t) s1] = { id, plain };
        else if (n2 > 0) paramQueue[(size_t) s2] = { id, plain };
        paramFifo.finishedWrite (n1 + n2);
    }

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi) override
    {
        onAudioThread = true;
        const int frames = buffer.getNumSamples();
        if (! plugin || ! active || frames > scratchOut.getNumSamples())
        {
            buffer.clear();
            midi.clear();
            onAudioThread = false;
            return;
        }
        if (! processing.load())
            processing = plugin->start_processing (plugin);

        inEvents.clear();
        outEvents.clear();

        // Parameter changes from the window, automation and modulators.
        {
            int s1, n1, s2, n2;
            paramFifo.prepareToRead (paramFifo.getNumReady(), s1, n1, s2, n2);
            auto push = [this] (const PendingParam& p)
            {
                clap_event_param_value_t e {};
                e.header = { sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
                e.param_id = p.id;
                e.cookie = nullptr;
                e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
                e.value = p.value;
                inEvents.push (&e.header);
            };
            for (int i = 0; i < n1; ++i) push (paramQueue[(size_t) (s1 + i)]);
            for (int i = 0; i < n2; ++i) push (paramQueue[(size_t) (s2 + i)]);
            paramFifo.finishedRead (n1 + n2);
        }

        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            const auto time = (uint32_t) jlimit (0, frames - 1, m.samplePosition);
            if (prefersClapNotes && (msg.isNoteOn() || msg.isNoteOff()))
            {
                clap_event_note_t e {};
                e.header = { sizeof e, time, CLAP_CORE_EVENT_SPACE_ID,
                             (uint16_t) (msg.isNoteOn() ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF), 0 };
                e.note_id = -1;
                e.port_index = 0;
                e.channel = (int16_t) (msg.getChannel() - 1);
                e.key = (int16_t) msg.getNoteNumber();
                e.velocity = msg.getFloatVelocity();
                inEvents.push (&e.header);
            }
            else if (msg.getRawDataSize() <= 3)
            {
                clap_event_midi_t e {};
                e.header = { sizeof e, time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0 };
                e.port_index = 0;
                for (int i = 0; i < msg.getRawDataSize(); ++i) e.data[i] = msg.getRawData()[i];
                inEvents.push (&e.header);
            }
        }
        inEvents.sortByTime();

        // Audio: the input is copied, since a plug-in may not expect to work in place.
        const int inChannels = getTotalNumInputChannels(), outChannels = getTotalNumOutputChannels();
        for (int c = 0; c < 2; ++c)
            scratchIn.copyFrom (c, 0, buffer, jmin (c, jmax (0, buffer.getNumChannels() - 1)), 0, frames);
        float* inPtrs[2] { scratchIn.getWritePointer (0), scratchIn.getWritePointer (1) };
        float* outPtrs[2] { scratchOut.getWritePointer (0), scratchOut.getWritePointer (1) };
        clap_audio_buffer_t in {}, out {};
        in.data32 = inPtrs; in.channel_count = 2;
        out.data32 = outPtrs; out.channel_count = 2;

        clap_event_transport_t transport {};
        fillTransport (transport);

        clap_process_t p {};
        p.steady_time = (int64_t) steadyTime;
        p.frames_count = (uint32_t) frames;
        p.transport = &transport;
        p.audio_inputs = inChannels > 0 ? &in : nullptr;
        p.audio_inputs_count = inChannels > 0 ? 1u : 0u;
        p.audio_outputs = outChannels > 0 ? &out : nullptr;
        p.audio_outputs_count = outChannels > 0 ? 1u : 0u;
        auto inList = inEvents.input();
        auto outList = outEvents.output();
        p.in_events = &inList;
        p.out_events = &outList;

        const auto status = plugin->process (plugin, &p);
        steadyTime += (uint64_t) frames;

        if (status == CLAP_PROCESS_ERROR)
            buffer.clear();
        else if (outChannels > 0)
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                buffer.copyFrom (c, 0, scratchOut, jmin (c, 1), 0, frames);

        midi.clear();
        for (uint32_t i = 0; i < outEvents.size(); ++i)
            handleOutput (outEvents.get (i), midi);
        onAudioThread = false;
    }

    void handleOutput (const clap_event_header_t* h, MidiBuffer& midi)
    {
        if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
        switch (h->type)
        {
            case CLAP_EVENT_NOTE_ON:
            case CLAP_EVENT_NOTE_OFF:
            {
                auto* e = reinterpret_cast<const clap_event_note_t*> (h);
                const int ch = jlimit (1, 16, e->channel + 1);
                midi.addEvent (h->type == CLAP_EVENT_NOTE_ON ? MidiMessage::noteOn (ch, e->key, (float) e->velocity)
                                                             : MidiMessage::noteOff (ch, e->key, (float) e->velocity),
                               (int) h->time);
                break;
            }
            case CLAP_EVENT_MIDI:
            {
                auto* e = reinterpret_cast<const clap_event_midi_t*> (h);
                midi.addEvent (e->data, MidiMessage::getMessageLengthFromFirstByte (e->data[0]), (int) h->time);
                break;
            }
            case CLAP_EVENT_PARAM_VALUE:
            {
                auto* e = reinterpret_cast<const clap_event_param_value_t*> (h);
                if (auto* prm = parameterFor (e->param_id))
                {
                    prm->normalised = prm->toNormal (e->value);
                    prm->changedByPlugin = true;
                }
                break;
            }
            case CLAP_EVENT_PARAM_GESTURE_BEGIN:
            case CLAP_EVENT_PARAM_GESTURE_END:
            {
                auto* e = reinterpret_cast<const clap_event_param_gesture_t*> (h);
                if (auto* prm = parameterFor (e->param_id))
                    prm->gesture = h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN ? 1 : 2;
                break;
            }
            default: break;
        }
    }

    void fillTransport (clap_event_transport_t& t)
    {
        t.header = { sizeof t, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0 };
        auto* head = getPlayHead();
        if (! head) return;
        const auto pos = head->getPosition();
        if (! pos) return;
        if (auto bpm = pos->getBpm()) { t.flags |= CLAP_TRANSPORT_HAS_TEMPO; t.tempo = *bpm; }
        if (auto ppq = pos->getPpqPosition())
        {
            t.flags |= CLAP_TRANSPORT_HAS_BEATS_TIMELINE;
            t.song_pos_beats = (clap_beattime) std::llround (*ppq * (double) CLAP_BEATTIME_FACTOR);
            if (auto bar = pos->getPpqPositionOfLastBarStart()) t.bar_start = (clap_beattime) std::llround (*bar * (double) CLAP_BEATTIME_FACTOR);
            if (auto bars = pos->getBarCount()) t.bar_number = (int32_t) *bars;
        }
        if (auto secs = pos->getTimeInSeconds())
        {
            t.flags |= CLAP_TRANSPORT_HAS_SECONDS_TIMELINE;
            t.song_pos_seconds = (clap_sectime) std::llround (*secs * (double) CLAP_SECTIME_FACTOR);
        }
        if (auto sig = pos->getTimeSignature())
        {
            t.flags |= CLAP_TRANSPORT_HAS_TIME_SIGNATURE;
            t.tsig_num = (uint16_t) sig->numerator;
            t.tsig_denom = (uint16_t) sig->denominator;
        }
        if (pos->getIsPlaying()) t.flags |= CLAP_TRANSPORT_IS_PLAYING;
        if (pos->getIsRecording()) t.flags |= CLAP_TRANSPORT_IS_RECORDING;
        if (pos->getIsLooping())
        {
            t.flags |= CLAP_TRANSPORT_IS_LOOP_ACTIVE;
            if (auto loop = pos->getLoopPoints())
            {
                t.loop_start_beats = (clap_beattime) std::llround (loop->ppqStart * (double) CLAP_BEATTIME_FACTOR);
                t.loop_end_beats = (clap_beattime) std::llround (loop->ppqEnd * (double) CLAP_BEATTIME_FACTOR);
            }
        }
    }

    //==========================================================================
    struct Stream { MemoryBlock* block; size_t read = 0; };

    void getStateInformation (MemoryBlock& out) override
    {
        if (! state || ! plugin) return;
        Stream s { &out };
        clap_ostream_t os { &s, [] (const clap_ostream_t* st, const void* data, uint64_t size) -> int64_t
        {
            static_cast<Stream*> (st->ctx)->block->append (data, (size_t) size);
            return (int64_t) size;
        } };
        state->save (plugin, &os);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (! state || ! plugin) return;
        MemoryBlock block (data, (size_t) size);
        Stream s { &block };
        clap_istream_t is { &s, [] (const clap_istream_t* st, void* buffer, uint64_t size) -> int64_t
        {
            auto* str = static_cast<Stream*> (st->ctx);
            const auto left = str->block->getSize() - str->read;
            const auto n = (size_t) jmin<uint64_t> (size, left);
            std::memcpy (buffer, static_cast<const char*> (str->block->getData()) + str->read, n);
            str->read += n;
            return (int64_t) n;
        } };
        state->load (plugin, &is);
        refreshParameterValues();
    }

    //==========================================================================
    void buildParameters()
    {
        if (! params) return;
        Array<AudioProcessorParameter*> none;
        const auto count = params->count (plugin);
        auto tree = std::make_unique<AudioProcessorParameterGroup>();
        for (uint32_t i = 0; i < count; ++i)
        {
            clap_param_info_t info {};
            if (! params->get_info (plugin, i, &info)) continue;
            if (info.flags & CLAP_PARAM_IS_HIDDEN) continue;
            double v = info.default_value;
            params->get_value (plugin, info.id, &v);
            auto p = std::make_unique<ClapParameter> (*this, info, v);
            byId[info.id] = p.get();
            tree->addChild (std::move (p));
        }
        setHostedParameterTree (std::move (*tree));
    }

    ClapParameter* parameterFor (clap_id id)
    {
        auto it = byId.find (id);
        return it == byId.end() ? nullptr : it->second;
    }

    void refreshParameterValues()
    {
        if (! params) return;
        for (auto& [id, p] : byId)
        {
            double v = 0;
            if (params->get_value (plugin, id, &v))
            {
                p->normalised = p->toNormal (v);
                p->changedByPlugin = true;
            }
        }
    }

    String valueToText (clap_id id, double v) const
    {
        char buf[256] {};
        if (params && params->value_to_text (plugin, id, v, buf, sizeof buf)) return String::fromUTF8 (buf);
        return String (v, 2);
    }

    double textToValue (clap_id id, const String& t, double fallback) const
    {
        double v = fallback;
        if (params && params->text_to_value (plugin, id, t.toRawUTF8(), &v)) return v;
        return t.getDoubleValue();
    }

    // Parameter changes the plug-in made are told to the host on the message thread.
    void timerCallback() override
    {
        for (auto& [id, p] : byId)
        {
            const auto g = p->gesture.exchange (0);
            if (g == 1) p->beginChangeGesture();
            if (p->changedByPlugin.exchange (false)) p->sendValueChangedMessageToListeners (p->normalised.load());
            if (g == 2) p->endChangeGesture();
        }
        // While stopped and not processing, parameters still need flushing.
        if (params && plugin && ! processing.load() && paramFifo.getNumReady() > 0)
        {
            inEvents.clear();
            outEvents.clear();
            int s1, n1, s2, n2;
            paramFifo.prepareToRead (paramFifo.getNumReady(), s1, n1, s2, n2);
            for (int i = 0; i < n1 + n2; ++i)
            {
                const auto& pp = paramQueue[(size_t) (i < n1 ? s1 + i : s2 + i - n1)];
                clap_event_param_value_t e {};
                e.header = { sizeof e, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0 };
                e.param_id = pp.id;
                e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
                e.value = pp.value;
                inEvents.push (&e.header);
            }
            paramFifo.finishedRead (n1 + n2);
            auto inList = inEvents.input();
            auto outList = outEvents.output();
            params->flush (plugin, &inList, &outList);
        }
    }

    //==========================================================================
    const void* hostExtension (const char* id)
    {
        static const clap_host_log_t log { [] (const clap_host_t*, clap_log_severity, const char* msg)
        {
            DBG ("CLAP: " << msg);
            ignoreUnused (msg);
        } };
        static const clap_host_thread_check_t threadCheck {
            [] (const clap_host_t*) { return MessageManager::existsAndIsCurrentThread(); },
            [] (const clap_host_t*) { return onAudioThread; } };
        static const clap_host_params_t hostParams {
            [] (const clap_host_t* h, clap_param_rescan_flags) { self (h)->refreshParameterValues(); },
            [] (const clap_host_t*, clap_id, clap_param_clear_flags) {},
            [] (const clap_host_t*) {} };
        static const clap_host_latency_t hostLatency { [] (const clap_host_t* h) { self (h)->requestRestart(); } };
        static const clap_host_state_t hostState { [] (const clap_host_t* h)
        {
            MessageManager::callAsync ([w = WeakReference<ClapInstance> (self (h))]
            {
                if (auto* i = w.get()) i->updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
            });
        } };
        static const clap_host_gui_t hostGui {
            [] (const clap_host_t*) {},
            [] (const clap_host_t* h, uint32_t w, uint32_t hh) { return self (h)->requestResize (w, hh); },
            [] (const clap_host_t*) { return true; },
            [] (const clap_host_t*) { return false; },
            [] (const clap_host_t*, bool) {} };
        static const clap_host_timer_support_t hostTimers {
            [] (const clap_host_t* h, uint32_t period, clap_id* id) { return self (h)->registerTimer (period, id); },
            [] (const clap_host_t* h, clap_id id) { return self (h)->unregisterTimer (id); } };
        static const clap_host_audio_ports_t hostPorts {
            [] (const clap_host_t*, uint32_t) { return false; },
            [] (const clap_host_t*, uint32_t) {} };
        static const clap_host_note_ports_t hostNotePorts {
            [] (const clap_host_t*) -> uint32_t { return CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI; },
            [] (const clap_host_t*, uint32_t) {} };

        const String ext (id);
        if (ext == CLAP_EXT_LOG) return &log;
        if (ext == CLAP_EXT_THREAD_CHECK) return &threadCheck;
        if (ext == CLAP_EXT_PARAMS) return &hostParams;
        if (ext == CLAP_EXT_LATENCY) return &hostLatency;
        if (ext == CLAP_EXT_STATE) return &hostState;
        if (ext == CLAP_EXT_GUI) return &hostGui;
        if (ext == CLAP_EXT_TIMER_SUPPORT) return &hostTimers;
        if (ext == CLAP_EXT_AUDIO_PORTS) return &hostPorts;
        if (ext == CLAP_EXT_NOTE_PORTS) return &hostNotePorts;
        return nullptr;
    }

    // A restart (after a latency change) is done on the message thread.
    void requestRestart()
    {
        MessageManager::callAsync ([w = WeakReference<ClapInstance> (this)]
        {
            auto* i = w.get();
            if (! i || ! i->plugin || ! i->active) return;
            const auto before = i->getLatencySamples();
            i->suspendProcessing (true);
            i->prepareToPlay (i->lastRate, i->lastBlock);
            i->suspendProcessing (false);
            if (i->getLatencySamples() != before)
                i->updateHostDisplay (ChangeDetails().withLatencyChanged (true));
        });
    }

    struct PluginTimer : public Timer
    {
        PluginTimer (ClapInstance& o, clap_id i) : owner (o), id (i) {}
        void timerCallback() override { if (owner.timers) owner.timers->on_timer (owner.plugin, id); }
        ClapInstance& owner;
        clap_id id;
    };

    bool registerTimer (uint32_t period, clap_id* id)
    {
        *id = nextTimer++;
        auto t = std::make_unique<PluginTimer> (*this, *id);
        t->startTimer ((int) jmax (1u, period));
        timerObjects[*id] = std::move (t);
        return true;
    }

    bool unregisterTimer (clap_id id)
    {
        auto it = timerObjects.find (id);
        if (it == timerObjects.end()) return false;
        it->second->stopTimer();
        timerObjects.erase (it);
        return true;
    }

    bool requestResize (uint32_t w, uint32_t h);
    void editorClosing();

    std::shared_ptr<Library> lib;
    PluginDescription description;
    clap_host_t host {};
    const clap_plugin_t* plugin = nullptr;
    const clap_plugin_params_t* params = nullptr;
    const clap_plugin_state_t* state = nullptr;
    const clap_plugin_latency_t* latency = nullptr;
    const clap_plugin_gui_t* gui = nullptr;
    const clap_plugin_tail_t* tail = nullptr;
    const clap_plugin_timer_support_t* timers = nullptr;
    const clap_plugin_note_ports_t* notePorts = nullptr;
    bool prefersClapNotes = false;

    bool active = false;
    std::atomic<bool> processing { false };
    double lastRate = 48000;
    int lastBlock = 512;
    uint64_t steadyTime = 0;

    AudioBuffer<float> scratchIn, scratchOut;
    EventList inEvents, outEvents;

    struct PendingParam { clap_id id; double value; };
    AbstractFifo paramFifo { 4096 };
    std::array<PendingParam, 4096> paramQueue {};
    std::map<clap_id, ClapParameter*> byId;

    std::map<clap_id, std::unique_ptr<PluginTimer>> timerObjects;
    clap_id nextTimer = 1;

    Component::SafePointer<AudioProcessorEditor> editorWindow;

    JUCE_DECLARE_WEAK_REFERENCEABLE (ClapInstance)
};

//==============================================================================
ClapParameter::ClapParameter (ClapInstance& o, const clap_param_info_t& info, double value)
    : owner (o), id (info.id), cookie (info.cookie), name (String::fromUTF8 (info.name)),
      min (info.min_value), max (info.max_value), defaultValue (info.default_value), flags (info.flags)
{
    normalised = toNormal (value);
}

void ClapParameter::setValue (float v)
{
    normalised = jlimit (0.0f, 1.0f, v);
    owner.queueParameter (id, toPlain (normalised));
}

String ClapParameter::getText (float v, int) const { return owner.valueToText (id, toPlain (v)); }
float ClapParameter::getValueForText (const String& t) const { return toNormal (owner.textToValue (id, t, toPlain (normalised))); }

//==============================================================================
// The plug-in's own window, inside the lower zone.
class ClapEditor : public AudioProcessorEditor
{
public:
    explicit ClapEditor (ClapInstance& i) : AudioProcessorEditor (i), owner (i), native (makeNativeHost())
    {
        addAndMakeVisible (native->component());
        created = owner.gui->create (owner.plugin, native->api(), false);
        if (! created) { setSize (300, 120); return; }
       #if ! JUCE_MAC
        owner.gui->set_scale (owner.plugin, 1.0);
       #endif
        uint32_t w = 400, h = 300;
        owner.gui->get_size (owner.plugin, &w, &h);
        resizable = owner.gui->can_resize (owner.plugin);
        setResizable (resizable, false);
        setSize ((int) w, (int) h);
        attach();
    }

    ~ClapEditor() override
    {
        if (created && owner.plugin)
        {
            owner.gui->hide (owner.plugin);
            owner.gui->destroy (owner.plugin);
        }
    }

    void paint (Graphics& g) override
    {
        if (! created)
        {
            g.fillAll (Colour (0xff1A1D23));
            g.setColour (Colour (0xffB0B6C0));
            g.drawFittedText ("This plug-in's window can't be shown here.", getLocalBounds(), Justification::centred, 2);
        }
    }

    void resized() override
    {
        native->component().setBounds (getLocalBounds());
        if (! created || ! attached || inResize) return;
        if (resizable)
        {
            auto w = (uint32_t) jmax (1, getWidth()), h = (uint32_t) jmax (1, getHeight());
            owner.gui->adjust_size (owner.plugin, &w, &h);
            owner.gui->set_size (owner.plugin, w, h);
        }
    }

    void parentHierarchyChanged() override { attach(); }
    void visibilityChanged() override { attach(); }

    void attach()
    {
        if (! created || attached) return;
        clap_window_t window {};
        window.api = native->api();
       #if JUCE_MAC
        window.cocoa = native->handle();
       #else
        if (! native->component().isShowing() || native->x11Window() == 0) return;
        window.x11 = native->x11Window();
       #endif
        if (owner.gui->set_parent (owner.plugin, &window))
        {
            attached = true;
            owner.gui->show (owner.plugin);
        }
    }

    bool pluginResize (uint32_t w, uint32_t h)
    {
        const ScopedValueSetter<bool> svs (inResize, true);
        setSize ((int) w, (int) h);
        return true;
    }

    ClapInstance& owner;
    std::unique_ptr<NativeHost> native;
    bool created = false, attached = false, resizable = false, inResize = false;
};

AudioProcessorEditor* ClapInstance::createEditor()
{
    if (! hasEditor()) return nullptr;
    auto* e = new ClapEditor (*this);
    editorWindow = e;
    return e;
}

bool ClapInstance::requestResize (uint32_t w, uint32_t h)
{
    MessageManager::callAsync ([sp = editorWindow, w, h]
    {
        if (auto* e = dynamic_cast<ClapEditor*> (sp.getComponent())) e->pluginResize (w, h);
    });
    return true;
}

void ClapInstance::editorClosing()
{
    if (auto* e = editorWindow.getComponent()) delete e;
}

} // namespace

//==============================================================================
#if ! JUCE_MAC
namespace
{
class GenericHost : public NativeHost
{
public:
   #if JUCE_LINUX || JUCE_BSD
    GenericHost() : embed (true, true) {}
    Component& component() override { return embed; }
    void* handle() override { return nullptr; }
    unsigned long x11Window() override { return embed.getHostWindowID(); }
    const char* api() const override { return CLAP_WINDOW_API_X11; }
    XEmbedComponent embed;
   #else
    Component& component() override { return blank; }
    void* handle() override { return nullptr; }
    const char* api() const override { return CLAP_WINDOW_API_WIN32; }
    Component blank;
   #endif
};
} // namespace

std::unique_ptr<NativeHost> makeNativeHost() { return std::make_unique<GenericHost>(); }
#endif

//==============================================================================
ClapPluginFormat::ClapPluginFormat() = default;
ClapPluginFormat::~ClapPluginFormat() = default;

void ClapPluginFormat::findAllTypesForFile (OwnedArray<PluginDescription>& results, const String& path)
{
    const File file (path);
    auto library = openLibrary (file);
    if (! library) return;
    auto* factory = library->factory();
    if (! factory) return;
    for (uint32_t i = 0; i < factory->get_plugin_count (factory); ++i)
    {
        auto* d = factory->get_plugin_descriptor (factory, i);
        if (! d || ! clap_version_is_compatible (d->clap_version)) continue;
        auto* desc = results.add (new PluginDescription());
        fillDescription (*desc, d, file);
    }
}

bool ClapPluginFormat::fileMightContainThisPluginType (const String& path)
{
    return File (path).hasFileExtension (".clap");
}

String ClapPluginFormat::getNameOfPluginFromIdentifier (const String& path)
{
    return File (path).getFileNameWithoutExtension();
}

bool ClapPluginFormat::doesPluginStillExist (const PluginDescription& d)
{
    return File (d.fileOrIdentifier).exists();
}

StringArray ClapPluginFormat::searchPathsForPlugins (const FileSearchPath& paths, bool recursive, bool)
{
    StringArray found;
    for (int i = 0; i < paths.getNumPaths(); ++i)
    {
        const auto dir = paths.getRawString (i);
        for (const auto& entry : RangedDirectoryIterator (File (dir), recursive, "*.clap",
                                                          File::findFilesAndDirectories))
        {
            const auto f = entry.getFile();
            if (f.hasFileExtension (".clap")) found.addIfNotAlreadyThere (f.getFullPathName());
        }
    }
    return found;
}

FileSearchPath ClapPluginFormat::getDefaultLocationsToSearch()
{
    FileSearchPath p;
   #if JUCE_MAC
    p.add (File ("~/Library/Audio/Plug-Ins/CLAP"));
    p.add (File ("/Library/Audio/Plug-Ins/CLAP"));
   #elif JUCE_WINDOWS
    p.add (File::getSpecialLocation (File::globalApplicationsDirectory).getChildFile ("Common Files/CLAP"));
   #else
    p.add (File ("~/.clap"));
    p.add (File ("/usr/lib/clap"));
   #endif
    const auto env = SystemStats::getEnvironmentVariable ("CLAP_PATH", {});
    for (auto& dir : StringArray::fromTokens (env, ":", ""))
        if (dir.isNotEmpty()) p.add (File (dir));
    p.removeNonExistentPaths();
    return p;
}

void ClapPluginFormat::createPluginInstance (const PluginDescription& desc, double sampleRate, int blockSize,
                                             PluginCreationCallback callback)
{
    auto library = openLibrary (File (desc.fileOrIdentifier));
    if (! library || ! library->factory())
    {
        callback (nullptr, "The CLAP plug-in couldn't be opened.");
        return;
    }
    auto* factory = library->factory();
    for (uint32_t i = 0; i < factory->get_plugin_count (factory); ++i)
    {
        auto* d = factory->get_plugin_descriptor (factory, i);
        if (! d || idHash (d->id) != desc.uniqueId) continue;
        auto instance = std::make_unique<ClapInstance> (library, d, desc);
        if (! instance->valid())
        {
            callback (nullptr, "The CLAP plug-in didn't start.");
            return;
        }
        instance->setRateAndBufferSizeDetails (sampleRate, blockSize);
        callback (std::move (instance), {});
        return;
    }
    callback (nullptr, "That plug-in isn't in its file any more.");
}

} // namespace kal::clap
