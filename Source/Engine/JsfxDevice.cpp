#include "JsfxDevice.h"

#include "EmbeddedLua.h"
#include "components/graphics_view.h"
#include "ysfx.h"

#include <array>
#include <atomic>
#include <cmath>

namespace kal
{
using namespace juce;

const std::vector<JsfxSpec>& jsfxDevices()
{
    static const std::vector<JsfxSpec> list {
        { "midi-evolver", "Midi Evolver", "Midi Evolver.jsfx", DeviceRole::MidiFx, 0, 430, 390, 0xff23272e,
          "Held chords grow into evolving motion: a bass, a drifting pad, repeated notes, an arpeggio." },
        { "transient-shuffler", "Transient Shuffler", "Transient Shuffler.jsfx", DeviceRole::AudioFx, 2, 410, 352, 0xff23272e,
          "One sample with the attack of another: drop two samples on it." },
        { "reese-basser", "Reese Basser", "Reese Basser.jsfx", DeviceRole::Instrument, 10, 480, 342, 0xff1c1f24,
          "Reese basses, eight ways of making one, tempo-locked beating." },
        { "reverberator", "Reverberator", "Reverberator.jsfx", DeviceRole::AudioFx, 30, 560, 360, 0xff23272e,
          "A reverb through odd materials instead of a room." },
        { "instrument-creator", "Instrument Creator", "Instrument Creator.jsfx", DeviceRole::Instrument, 10, 800, 480, 0xff1b1c20,
          "Instruments that don't exist, built from parts: a glass trumpet, a violin of ice." },
    };
    return list;
}

PluginRef jsfxRef (const JsfxSpec& s)
{
    PluginRef r;
    r.format = "Kalibre";
    r.identifier = std::string ("jsfx:") + s.id;
    r.name = s.name;
    r.manufacturer = "KallumS";
    r.isInstrument = s.role == DeviceRole::Instrument;
    return r;
}

namespace
{
class JsfxProcessor;

//==============================================================================
// One host parameter per slider the JSFX declares, hidden ones included, so
// automation and last-touched reach everything the window can change. The
// value the host sees is 0..1 along the slider's own curve.
class SliderParameter : public HostedAudioProcessorParameter
{
public:
    SliderParameter (JsfxProcessor& owner, ysfx_t* fx, int index)
        : owner (owner), index (index), name (sliderName (fx, index))
    {
        ysfx_slider_get_curve (fx, (uint32_t) index, &curve);
        if (ysfx_slider_is_enum (fx, (uint32_t) index))
        {
            const uint32_t n = ysfx_slider_get_enum_size (fx, (uint32_t) index);
            for (uint32_t i = 0; i < n; ++i) names.add (CharPointer_UTF8 (ysfx_slider_get_enum_name (fx, (uint32_t) index, i)));
        }
        def = (float) fromYsfx (curve.def);
        value = toYsfx (def);
    }

    double toYsfx (double norm) const
    {
        double v = ysfx_normalized_to_ysfx_value (jlimit (0.0, 1.0, norm), &curve);
        if (curve.inc > 0) v = curve.min + std::round ((v - curve.min) / curve.inc) * curve.inc;
        return jlimit (jmin (curve.min, curve.max), jmax (curve.min, curve.max), v);
    }
    double fromYsfx (double v) const { return jlimit (0.0, 1.0, (double) ysfx_ysfx_value_to_normalized (v, &curve)); }

    float getValue() const override { return (float) fromYsfx (value.load()); }
    void setValue (float v) override;
    double sliderValue() const { return value.load(); }
    void setSliderValue (double v) { value.store (v); }
    float getDefaultValue() const override { return def; }
    bool isDiscrete() const override { return ! names.isEmpty(); }
    int getNumSteps() const override
    {
        if (! names.isEmpty()) return names.size();
        if (curve.inc > 0) return jmax (2, (int) std::round (std::abs (curve.max - curve.min) / curve.inc) + 1);
        return AudioProcessor::getDefaultNumParameterSteps();
    }
    String getText (float norm, int maxLen) const override
    {
        const double v = toYsfx (norm);
        String s;
        if (! names.isEmpty()) s = names[jlimit (0, names.size() - 1, (int) std::lround (v))];
        else s = String (v, curve.inc >= 1 ? 0 : curve.inc >= 0.1 ? 1 : 2);
        return maxLen > 0 ? s.substring (0, maxLen) : s;
    }
    float getValueForText (const String& text) const override
    {
        const int i = names.indexOf (text.trim(), true);
        return (float) fromYsfx (i >= 0 ? (double) i : text.getDoubleValue());
    }

    bool syncing = false;   // set while the device itself moves it, so it isn't sent back
    String getParameterID() const override { return "slider" + String (index + 1); }
    String getName (int max) const override { return name.substring (0, max); }
    String getLabel() const override { return {}; }

private:
    static String sliderName (ysfx_t* fx, int index)
    {
        String n = String (CharPointer_UTF8 (ysfx_slider_get_name (fx, (uint32_t) index))).trimCharactersAtStart ("-").trim();
        return n.isEmpty() ? "Slider " + String (index + 1) : n;
    }

    JsfxProcessor& owner;
    int index;
    String name;
    ysfx_slider_curve_t curve {};
    StringArray names;
    float def = 0;
    std::atomic<double> value { 0 };
};

//==============================================================================
class JsfxProcessor : public AudioPluginInstance, private Timer
{
public:
    explicit JsfxProcessor (const JsfxSpec& s) : AudioPluginInstance (busesFor (s.role)), spec (s)
    {
        const File path = writeJsfx();
        ysfx_config_u config { ysfx_config_new() };
        ysfx_register_builtin_audio_formats (config.get());
        ysfx_guess_file_roots (config.get(), path.getFullPathName().toRawUTF8());
        auto log = [] (intptr_t user, ysfx_log_level level, const char* message)
        {
            if (level == ysfx_log_error) *(String*) user += String (CharPointer_UTF8 (message)) + "\n";
        };
        ysfx_set_log_reporter (config.get(), +log);
        ysfx_set_user_data (config.get(), (intptr_t) &error);
        fx = ysfx_new (config.get());
        const bool ok = path.existsAsFile() && ysfx_load_file (fx, path.getFullPathName().toRawUTF8(), 0) && ysfx_compile (fx, 0);
        if (! ok && error.isEmpty()) error = String (spec.name) + " couldn't be loaded.";
        if (ok)
        {
            ysfx_set_sample_rate (fx, 48000);
            ysfx_set_block_size (fx, 512);
            ysfx_init (fx);
            // Once through @serialize, so what it saves is what it would load.
            if (ysfx_state_u fresh { ysfx_save_state (fx) }) ysfx_load_state (fx, fresh.get());
            for (int i = 0; i < ysfx_max_sliders; ++i)
            {
                if (! ysfx_slider_exists (fx, (uint32_t) i)) continue;
                auto param = std::make_unique<SliderParameter> (*this, fx, i);
                params[(size_t) i] = param.get();
                addHostedParameter (std::move (param));
                syncFromSlider (i);
            }
        }
        startTimerHz (30);
    }

    ~JsfxProcessor() override
    {
        stopTimer();
        ysfx_free (fx);
    }

    ysfx_t* effect() const { return fx; }
    const String& loadError() const { return error; }
    const JsfxSpec& specification() const { return spec; }
    int editorWidth = 0, editorHeight = 0;   // the window's size, kept with the project

    void markHostChange (int slider)
    {
        const uint8_t g = ysfx_fetch_slider_group_index ((uint32_t) slider);
        hostChanged[g].fetch_or (ysfx_slider_mask ((uint32_t) slider, g));
    }

    //==========================================================================
    const String getName() const override { return spec.name; }
    void fillInPluginDescription (PluginDescription& d) const override
    {
        d.name = d.descriptiveName = spec.name;
        d.pluginFormatName = "Kalibre";
        d.manufacturerName = "KallumS";
        d.fileOrIdentifier = String ("jsfx:") + spec.id;
        d.isInstrument = spec.role == DeviceRole::Instrument;
        d.numInputChannels = getTotalNumInputChannels();
        d.numOutputChannels = getTotalNumOutputChannels();
    }

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        if (spec.role == DeviceRole::MidiFx) return l.inputBuses.isEmpty() && l.outputBuses.isEmpty();
        if (l.getMainOutputChannelSet() != AudioChannelSet::stereo()) return false;
        return spec.role == DeviceRole::Instrument ? l.inputBuses.isEmpty() : l.getMainInputChannelSet() == AudioChannelSet::stereo();
    }

    void prepareToPlay (double rate, int block) override
    {
        const ScopedLock sl (lock);
        if (error.isNotEmpty()) return;
        ysfx_set_sample_rate (fx, rate);
        ysfx_set_block_size (fx, (uint32_t) block);
        ysfx_init (fx);
        setLatencySamples ((int) std::lround (ysfx_get_pdc_delay (fx)));
    }
    void releaseResources() override {}

    void processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi) override
    {
        ScopedNoDenormals noDenormals;
        const ScopedTryLock sl (lock);
        if (! sl.isLocked() || error.isNotEmpty()) { buffer.clear(); midi.clear(); return; }
        applyHostChanges();
        // Where the song is, for the JSFX's play_state, tempo and beat_position.
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                const auto t = pos->getTimeInSamples();
                if (pos->getIsRecording()) time.playback_state = ysfx_playback_recording;
                else if (pos->getIsPlaying()) time.playback_state = ysfx_playback_playing;
                else time.playback_state = (t && *t == lastPosition) ? ysfx_playback_stopped : ysfx_playback_paused;
                if (t) lastPosition = *t;
                if (auto bpm = pos->getBpm()) time.tempo = *bpm;
                if (auto s = pos->getTimeInSeconds()) time.time_position = *s;
                if (auto q = pos->getPpqPosition()) time.beat_position = *q;
                if (auto sig = pos->getTimeSignature()) { time.time_signature[0] = (uint32_t) sig->numerator; time.time_signature[1] = (uint32_t) sig->denominator; }
            }
        ysfx_set_time_info (fx, &time);
        for (const auto md : midi)
        {
            ysfx_midi_event_t ev {};
            ev.offset = (uint32_t) md.samplePosition;
            ev.size = (uint32_t) md.numBytes;
            ev.data = md.data;
            ysfx_send_midi (fx, &ev);
        }
        midi.clear();
        ysfx_process_float (fx, buffer.getArrayOfReadPointers(), buffer.getArrayOfWritePointers(),
                            (uint32_t) getTotalNumInputChannels(), (uint32_t) getTotalNumOutputChannels(), (uint32_t) buffer.getNumSamples());
        // What it sends goes on: Midi Evolver's notes to the instrument after it.
        ysfx_midi_event_t out;
        while (ysfx_receive_midi (fx, &out)) midi.addEvent (out.data, (int) out.size, (int) jmin<uint32_t> (out.offset, (uint32_t) jmax (0, buffer.getNumSamples() - 1)));
        for (uint8_t g = 0; g < ysfx_max_slider_groups; ++g)
            scriptChanged[g].fetch_or (ysfx_fetch_slider_automations (fx, g) | ysfx_fetch_slider_changes (fx, g));
    }

    double getTailLengthSeconds() const override { return spec.tailSeconds; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return spec.role == DeviceRole::MidiFx; }
    AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}

    // Every slider and the JSFX's own @serialize data, as REAPER keeps them, and the window's size.
    void getStateInformation (MemoryBlock& dest) override
    {
        if (error.isNotEmpty()) return;
        ysfx_state_u state;
        {
            const ScopedLock sl (lock);
            applyHostChanges();
            state.reset (ysfx_save_state (fx));
        }
        ValueTree root ("jsfx");
        root.setProperty ("version", 1, nullptr);
        root.setProperty ("editorWidth", editorWidth, nullptr);
        root.setProperty ("editorHeight", editorHeight, nullptr);
        if (state)
        {
            ValueTree sliders ("sliders");
            for (uint32_t i = 0; i < state->slider_count; ++i) sliders.setProperty (String (state->sliders[i].index), state->sliders[i].value, nullptr);
            root.addChild (sliders, -1, nullptr);
            root.setProperty ("data", Base64::toBase64 (state->data, state->data_size), nullptr);
        }
        MemoryOutputStream stream (dest, false);
        root.writeToStream (stream);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (error.isNotEmpty()) return;
        const auto root = ValueTree::readFromData (data, (size_t) size);
        if (! root.hasType ("jsfx") || (int) root.getProperty ("version") != 1) return;
        editorWidth = root.getProperty ("editorWidth", 0);
        editorHeight = root.getProperty ("editorHeight", 0);
        std::vector<ysfx_state_slider_t> sliders;
        const auto st = root.getChildWithName ("sliders");
        for (int i = 0; i < ysfx_max_sliders; ++i)
            if (const var* v = st.getPropertyPointer (String (i))) sliders.push_back ({ (uint32_t) i, (ysfx_real) (double) *v });
        MemoryBlock blob;
        {
            MemoryOutputStream out (blob, false);
            Base64::convertFromBase64 (out, root.getProperty ("data").toString());
        }
        ysfx_state_t state {};
        state.sliders = sliders.data();
        state.slider_count = (uint32_t) sliders.size();
        state.data = (uint8_t*) blob.getData();
        state.data_size = blob.getSize();
        {
            const ScopedLock sl (lock);
            ysfx_load_state (fx, &state);
            for (auto& bits : hostChanged) bits.store (0);
        }
        for (int i = 0; i < ysfx_max_sliders; ++i) syncFromSlider (i);
        updateHostDisplay (AudioProcessorListener::ChangeDetails().withProgramChanged (true));
    }

private:
    const JsfxSpec& spec;
    ysfx_t* fx = nullptr;
    String error;
    CriticalSection lock;   // the effect: the audio thread against state calls
    std::array<SliderParameter*, ysfx_max_sliders> params {};
    std::array<std::atomic<uint64_t>, ysfx_max_slider_groups> hostChanged {}, scriptChanged {};
    std::array<int, ysfx_max_sliders> drift {};
    int tick = 0;
    int64_t lastPosition = -1;
    ysfx_time_info_t time {};

    static BusesProperties busesFor (DeviceRole role)
    {
        if (role == DeviceRole::MidiFx) return BusesProperties();
        if (role == DeviceRole::Instrument) return BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true);
        return BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true).withOutput ("Output", AudioChannelSet::stereo(), true);
    }

    // ysfx loads from a path (where it looks for imports and data), so the
    // file carried in the app is written to Kalibre's own folder first.
    File writeJsfx() const
    {
        const embedded::File* found = nullptr;
        for (auto& f : embedded::files()) if (String (f.name) == "jsfx/" + String (spec.file)) found = &f;
        if (! found) return {};
        auto dir = File::getSpecialLocation (File::userApplicationDataDirectory)
       #if JUCE_MAC
                       .getChildFile ("Application Support")
       #endif
                       .getChildFile ("Kalibre").getChildFile ("JSFX");
        dir.createDirectory();
        const auto out = dir.getChildFile (spec.file);
        MemoryBlock want (found->data, found->size), have;
        if (! out.loadFileAsData (have) || have != want) out.replaceWithData (found->data, found->size);
        return out;
    }

    void syncFromSlider (int i)
    {
        if (auto* p = params[(size_t) i]) p->setSliderValue (ysfx_slider_get_value (fx, (uint32_t) i));
    }

    // The host's changes become slider changes (and run @slider). With the lock held.
    void applyHostChanges()
    {
        for (uint8_t g = 0; g < ysfx_max_slider_groups; ++g)
        {
            uint64_t bits = hostChanged[g].exchange (0);
            for (int b = 0; bits && b < 64; ++b, bits >>= 1)
                if ((bits & 1) != 0)
                    if (auto* p = params[(size_t) (g * 64 + b)]) ysfx_slider_set_value (fx, (uint32_t) (g * 64 + b), p->sliderValue(), true);
        }
    }

    // Thirty times a second: sliders the window moved are told to the host,
    // so automation records them and the project saves them.
    void timerCallback() override
    {
        if (error.isNotEmpty()) return;
        for (uint8_t g = 0; g < ysfx_max_slider_groups; ++g)
        {
            uint64_t bits = scriptChanged[g].exchange (0);
            for (int b = 0; bits && b < 64; ++b, bits >>= 1)
            {
                if ((bits & 1) == 0) continue;
                const int i = g * 64 + b;
                auto* p = params[(size_t) i];
                if (! p) continue;
                const double v = ysfx_slider_get_value (fx, (uint32_t) i);
                if (v != p->sliderValue())
                {
                    p->syncing = true;
                    p->beginChangeGesture();
                    p->setValueNotifyingHost ((float) p->fromYsfx (v));
                    p->endChangeGesture();
                    p->setSliderValue (v);
                    p->syncing = false;
                }
                drift[(size_t) i] = 0;
            }
        }
        if (++tick % 5 != 0) return;
        for (int i = 0; i < ysfx_max_sliders; ++i)
        {
            auto* p = params[(size_t) i];
            if (! p) continue;
            const uint8_t g = ysfx_fetch_slider_group_index ((uint32_t) i);
            const bool pending = (hostChanged[g].load() & ysfx_slider_mask ((uint32_t) i, g)) != 0;
            const double v = ysfx_slider_get_value (fx, (uint32_t) i);
            if (pending || v == p->sliderValue()) { drift[(size_t) i] = 0; continue; }
            if (++drift[(size_t) i] >= 2)
            {
                p->syncing = true;
                p->setValueNotifyingHost ((float) p->fromYsfx (v));
                p->setSliderValue (v);
                p->syncing = false;
                drift[(size_t) i] = 0;
            }
        }
        const int latency = (int) std::lround (ysfx_get_pdc_delay (fx));
        if (latency != getLatencySamples()) setLatencySamples (latency);
    }
};

void SliderParameter::setValue (float v)
{
    value.store (toYsfx (v));
    if (! syncing) owner.markHostChange (index);
}

//==============================================================================
// The window: the JSFX's @gfx and nothing else, at the size it asks for.
class JsfxEditor : public AudioProcessorEditor
{
public:
    explicit JsfxEditor (JsfxProcessor& p) : AudioProcessorEditor (p), proc (p)
    {
        const auto& s = p.specification();
        uint32_t dim[2] { (uint32_t) s.minWidth, (uint32_t) s.minHeight };
        if (p.loadError().isEmpty())
        {
            view = std::make_unique<YsfxGraphicsView>();
            view->setEffect (p.effect());
            addAndMakeVisible (*view);
            ysfx_get_gfx_dim (p.effect(), dim);
        }
        else
        {
            message.setText (p.loadError(), dontSendNotification);
            message.setJustificationType (Justification::centred);
            message.setColour (Label::textColourId, Colour (0xffdde1e7));
            addAndMakeVisible (message);
        }
        const int w = p.editorWidth > 0 ? p.editorWidth : (int) jmax<uint32_t> (dim[0], (uint32_t) s.minWidth);
        const int h = p.editorHeight > 0 ? p.editorHeight : (int) jmax<uint32_t> (dim[1], (uint32_t) s.minHeight);
        setResizable (true, false);
        setResizeLimits (s.minWidth, s.minHeight, 4096, 4096);
        setSize (w, h);
    }

    void paint (Graphics& g) override { g.fillAll (Colour (proc.specification().background)); }
    void resized() override
    {
        if (view) view->setBounds (getLocalBounds());
        message.setBounds (getLocalBounds().reduced (24));
    }

private:
    JsfxProcessor& proc;
    std::unique_ptr<YsfxGraphicsView> view;
    Label message;
};

AudioProcessorEditor* JsfxProcessor::createEditor() { return new JsfxEditor (*this); }
} // namespace

std::unique_ptr<AudioPluginInstance> makeJsfx (const PluginRef& ref)
{
    const String id (ref.identifier);
    if (! id.startsWith ("jsfx:")) return nullptr;
    for (auto& s : jsfxDevices())
        if (id.fromFirstOccurrenceOf (":", false, false) == s.id) return std::make_unique<JsfxProcessor> (s);
    return nullptr;
}

} // namespace kal
