#include "BuiltinInstruments.h"
#include "Trombolese.h"

#include "Theme.h"

namespace kal
{
using namespace juce;

PluginRef synthRef()
{
    PluginRef r;
    r.format = "Kalibre";
    r.identifier = "synth";
    r.name = "Kalibre Synth";
    r.manufacturer = "Kalibre";
    r.isInstrument = true;
    return r;
}

PluginRef tromboleseRef()
{
    PluginRef r = synthRef();
    r.identifier = "trombolese";
    r.name = "Trombolese";
    return r;
}

PluginRef samplerRef()
{
    PluginRef r = synthRef();
    r.identifier = "sampler";
    r.name = "Kalibre Sampler";
    return r;
}

std::unique_ptr<AudioPluginInstance> makeInstrument (const PluginRef& ref)
{
    if (ref.identifier == "synth") return std::make_unique<KalibreSynth>();
    if (ref.identifier == "sampler") return std::make_unique<KalibreSampler>();
    if (ref.identifier == "trombolese") return std::make_unique<TromboleseInstrument>();
    return nullptr;
}

//==============================================================================
namespace
{
// A low-pass state-variable filter (topology-preserving), one sample.
inline float lowPass (float x, float cutoff, float resonance, double rate, float& ic1, float& ic2)
{
    const float g = std::tan (MathConstants<float>::pi * jlimit (20.0f, (float) rate * 0.45f, cutoff) / (float) rate);
    const float k = 2.0f - 1.96f * jlimit (0.0f, 1.0f, resonance);
    const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    return v2;
}

// Band-limited steps (polyBLEP), so saws and squares don't alias.
inline double blep (double t, double dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

inline double oscillator (int wave, double t, double dt)
{
    switch (wave)
    {
        case 0:  return 2.0 * t - 1.0 - blep (t, dt);                                                  // saw
        case 1:  return (t < 0.5 ? 1.0 : -1.0) + blep (t, dt) - blep (std::fmod (t + 0.5, 1.0), dt);   // square
        case 2:  return 4.0 * std::abs (t - 0.5) - 1.0;                                                 // triangle
        default: return std::sin (t * MathConstants<double>::twoPi);                                    // sine
    }
}

ADSR::Parameters adsr (float a, float d, float s, float r) { return { a, d, s, r }; }

//==============================================================================
// Knobs in groups, laid out to whatever size the lower zone gives.
class KnobPanel : public Component
{
public:
    explicit KnobPanel (BuiltinInstrument& p)
    {
        for (auto& g : p.groups)
        {
            auto& group = groups.emplace_back();
            group.name = g.name;
            for (auto* prm : g.params)
            {
                auto* s = knobs.add (new Slider (Slider::RotaryHorizontalVerticalDrag, Slider::TextBoxBelow));
                s->setTextBoxStyle (Slider::TextBoxBelow, false, 64, 14);
                s->setTitle (prm->getName (64));
                s->setTooltip (prm->getName (64));
                addAndMakeVisible (s);
                attachments.add (new SliderParameterAttachment (*prm, *s));
                auto* l = labels.add (new Label ({}, prm->getName (32)));
                l->setJustificationType (Justification::centred);
                l->setFont (theme::font (theme::smallPoints));
                l->setColour (Label::textColourId, theme::textSecondary);
                addAndMakeVisible (l);
                group.indices.push_back (knobs.size() - 1);
            }
        }
    }

    void paint (Graphics& g) override
    {
        g.fillAll (theme::sunken);
        g.setFont (theme::font (theme::smallPoints, true));
        for (auto& gr : groups)
        {
            g.setColour (theme::lane);
            g.fillRoundedRectangle (gr.box.toFloat(), 5.0f);
            g.setColour (theme::text);
            g.drawText (gr.name, gr.box.removeFromTop (18).reduced (8, 0), Justification::centredLeft, false);
        }
    }

    void resized() override
    {
        // Each group a box across; its knobs in two rows.
        auto r = getLocalBounds().reduced (6);
        int total = 0;
        for (auto& gr : groups) total += ((int) gr.indices.size() + 1) / 2;
        const int unit = total > 0 ? (r.getWidth() - 6 * ((int) groups.size() - 1)) / total : 0;
        for (auto& gr : groups)
        {
            const int cols = ((int) gr.indices.size() + 1) / 2;
            gr.box = r.removeFromLeft (unit * cols);
            r.removeFromLeft (6);
            auto inner = gr.box.withTrimmedTop (18).reduced (4);
            const int rowH = inner.getHeight() / 2;
            for (size_t i = 0; i < gr.indices.size(); ++i)
            {
                const int col = (int) i % cols, row = (int) i / cols;
                Rectangle<int> cell (inner.getX() + col * unit, inner.getY() + row * rowH, unit, rowH);
                labels[gr.indices[i]]->setBounds (cell.removeFromTop (14));
                knobs[gr.indices[i]]->setBounds (cell.reduced (2));
            }
        }
    }

private:
    struct G { String name; std::vector<int> indices; Rectangle<int> box; };
    std::vector<G> groups;
    OwnedArray<Slider> knobs;
    OwnedArray<Label> labels;
    OwnedArray<SliderParameterAttachment> attachments;
};

class SynthEditor : public AudioProcessorEditor
{
public:
    explicit SynthEditor (KalibreSynth& p) : AudioProcessorEditor (p), knobs (p)
    {
        addAndMakeVisible (knobs);
        setResizable (true, false);
        setResizeLimits (360, 160, 3000, 1600);
        setSize (900, 260);
    }
    void resized() override { knobs.setBounds (getLocalBounds()); }

private:
    KnobPanel knobs;
};

class SamplerEditor : public AudioProcessorEditor,
                      public FileDragAndDropTarget,
                      public DragAndDropTarget,
                      private Timer
{
public:
    explicit SamplerEditor (KalibreSampler& p) : AudioProcessorEditor (p), proc (p), knobs (p)
    {
        addAndMakeVisible (knobs);
        addAndMakeVisible (loadButton);
        loadButton.onClick = [this]
        {
            chooser = std::make_unique<FileChooser> ("Choose a sample", proc.file().getParentDirectory(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
            {
                if (fc.getResult().existsAsFile()) loadFile (fc.getResult());
            });
        };
        proc.onLoaded = [sp = SafePointer<SamplerEditor> (this)] { if (sp) sp->repaint(); };
        setResizable (true, false);
        setResizeLimits (360, 200, 3000, 1600);
        setSize (1000, 300);
        startTimerHz (10);
    }
    ~SamplerEditor() override { proc.onLoaded = nullptr; }

    // The marks follow the knobs: drawn again when one that moves them changes.
    void timerCallback() override
    {
        String now;
        for (int i : { (int) KalibreSampler::Start, (int) KalibreSampler::End, (int) KalibreSampler::Loop, (int) KalibreSampler::LoopStart,
                       (int) KalibreSampler::Mode, (int) KalibreSampler::Sensitivity, (int) KalibreSampler::Position })
            now << proc.value (i) << ",";
        if (now != shown) { shown = now; slices = proc.mode() == KalibreSampler::PlayMode::Slices ? proc.slicePoints() : std::vector<int>(); repaint(); }
    }

    void loadFile (const File& f)
    {
        String error;
        if (! proc.load (f, error))
        {
            // Asked inside the main window, never in one of its own (decision 0002).
            auto* w = new AlertWindow ("The sample couldn't be loaded", error, MessageBoxIconType::NoIcon);
            w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
            ui::present (w, nullptr, true);
        }
        repaint();
    }

    void paint (Graphics& g) override
    {
        g.fillAll (theme::sunken);
        auto w = waveArea();
        g.setColour (theme::roll);
        g.fillRect (w);
        const auto& data = proc.sample();
        g.setFont (theme::font (theme::smallPoints));
        if (data.getNumSamples() == 0)
        {
            g.setColour (theme::textSecondary);
            g.drawFittedText ("Drop a sample here, from the browser or the Finder, or click Load Sample.", w, Justification::centred, 2);
            return;
        }
        const int n = data.getNumSamples();
        g.setColour (theme::control);
        for (int x = 0; x < w.getWidth(); ++x)
        {
            const int a = (int) ((int64) x * n / w.getWidth()), b = jmax (a + 1, (int) ((int64) (x + 1) * n / w.getWidth()));
            auto mm = data.findMinMax (0, a, jmin (b, n) - a);
            g.drawVerticalLine (w.getX() + x, (float) w.getCentreY() - mm.getEnd() * (float) w.getHeight() * 0.48f,
                                (float) w.getCentreY() - mm.getStart() * (float) w.getHeight() * 0.48f + 1.0f);
        }
        auto mark = [&] (float v, Colour c)
        {
            g.setColour (c);
            g.fillRect ((float) w.getX() + v * (float) w.getWidth() - 1.0f, (float) w.getY(), 2.0f, (float) w.getHeight());
        };
        // Slices: a line where each starts, numbered from the root note (0095).
        const auto how = proc.mode();
        if (how == KalibreSampler::PlayMode::Slices)
            for (size_t k = 0; k < slices.size(); ++k)
            {
                const float x = (float) w.getX() + (float) slices[k] / (float) n * (float) w.getWidth();
                g.setColour (theme::edge);
                g.fillRect (x, (float) w.getY(), 1.0f, (float) w.getHeight());
                g.setColour (theme::textSecondary);
                g.drawText (MidiMessage::getMidiNoteName (roundToInt (proc.value (KalibreSampler::Root)) + (int) k, true, true, 4),
                            Rectangle<float> (x + 2.0f, (float) w.getBottom() - 16.0f, 40.0f, 14.0f), Justification::centredLeft, false);
            }
        if (how == KalibreSampler::PlayMode::Granular)
        {
            const float s0 = proc.value (KalibreSampler::Start), s1 = proc.value (KalibreSampler::End);
            mark (s0 + proc.value (KalibreSampler::Position) * (s1 - s0), theme::edge);
        }
        mark (proc.value (KalibreSampler::Start), theme::playhead);
        mark (proc.value (KalibreSampler::End), theme::playhead);
        if (proc.value (KalibreSampler::Loop) > 0.5f) mark (proc.value (KalibreSampler::LoopStart), theme::edge);
        g.setColour (theme::text);
        const String modeName = how == KalibreSampler::PlayMode::Slices ? "Slices: " + String ((int) slices.size()) + ", a key each from the root"
                              : how == KalibreSampler::PlayMode::Granular ? String ("Granular: the grains start at Position")
                              : String ("Keys: across the keyboard from the root");
        g.drawText (proc.file().getFileName() + "   " + modeName, w.reduced (6, 4), Justification::topLeft, true);
    }

    Rectangle<int> waveArea() const { return getLocalBounds().removeFromTop (getHeight() * 2 / 5).reduced (6).withTrimmedRight (loadButton.getWidth() + 8); }

    void resized() override
    {
        auto top = getLocalBounds().removeFromTop (getHeight() * 2 / 5).reduced (6);
        loadButton.setBounds (top.removeFromRight (loadButton.idealWidth (theme::smallControl)).removeFromTop (theme::px (24)));
        knobs.setBounds (getLocalBounds().withTrimmedTop (getHeight() * 2 / 5));
    }

    bool isInterestedInFileDrag (const StringArray& files) override { return files.size() == 1; }
    void filesDropped (const StringArray& files, int, int) override { loadFile (File (files[0])); }
    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description.toString().startsWith ("file:"); }
    void itemDropped (const SourceDetails& d) override { loadFile (File (d.description.toString().fromFirstOccurrenceOf ("file:", false, false))); }

private:
    KalibreSampler& proc;
    String shown;
    std::vector<int> slices;
    KnobPanel knobs;
    theme::HouseButton loadButton { ui::u8 ("Load Sample\xe2\x80\xa6"), "Choose the sample to play." };
    std::unique_ptr<FileChooser> chooser;
};
} // namespace

//==============================================================================
BuiltinInstrument::BuiltinInstrument()
    : AudioPluginInstance (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true))
{
}

void BuiltinInstrument::addFloat (const String& group, const String& id, const String& name, NormalisableRange<float> range, float def, const String& suffix)
{
    // Readable values: as many decimals as the size needs, a note's name for a root.
    auto text = [suffix, id] (float v, int)
    {
        if (id == "root") return MidiMessage::getMidiNoteName (roundToInt (v), true, true, 4);
        const float a = std::abs (v);
        const int places = a >= 100.0f ? 0 : a >= 10.0f ? 1 : 2;
        return String (v, places) + suffix;
    };
    auto p = std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def,
                                                    AudioParameterFloatAttributes().withLabel (suffix).withStringFromValueFunction (text));
    params.push_back (p.get());
    if (groups.empty() || groups.back().name != group) groups.push_back ({ group, {} });
    groups.back().params.push_back (p.get());
    addHostedParameter (std::move (p));
}

void BuiltinInstrument::addChoice (const String& group, const String& id, const String& name, StringArray choices, int def)
{
    auto p = std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, choices, def);
    params.push_back (p.get());
    if (groups.empty() || groups.back().name != group) groups.push_back ({ group, {} });
    groups.back().params.push_back (p.get());
    addHostedParameter (std::move (p));
}

void BuiltinInstrument::getStateInformation (MemoryBlock& m)
{
    XmlElement xml ("KalibreInstrument");
    for (auto* p : params) xml.setAttribute (p->getParameterID(), p->getValue());
    writeExtra (xml);
    copyXmlToBinary (xml, m);
}

void BuiltinInstrument::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (! xml) return;
    for (auto* p : params)
        if (xml->hasAttribute (p->getParameterID())) p->setValueNotifyingHost ((float) xml->getDoubleAttribute (p->getParameterID()));
    readExtra (*xml);
}

//==============================================================================
KalibreSynth::KalibreSynth()
{
    const StringArray waves { "Saw", "Square", "Triangle", "Sine" };
    auto time = [] (float top) { return NormalisableRange<float> (0.001f, top, 0.0f, 0.3f); };
    addChoice ("Oscillators", "wave1", "Wave 1", waves, 0);
    addChoice ("Oscillators", "wave2", "Wave 2", waves, 1);
    addFloat ("Oscillators", "detune", "Osc 2 Pitch", { -24.0f, 24.0f, 1.0f }, 0.0f, " st");
    addFloat ("Oscillators", "fine", "Osc 2 Fine", { -50.0f, 50.0f }, 7.0f, " ct");
    addFloat ("Oscillators", "mix", "Osc Mix", { 0.0f, 1.0f }, 0.5f);
    addFloat ("Oscillators", "noise", "Noise", { 0.0f, 1.0f }, 0.0f);
    addFloat ("Filter", "cutoff", "Cutoff", { 30.0f, 18000.0f, 0.0f, 0.25f }, 2400.0f, " Hz");
    addFloat ("Filter", "resonance", "Resonance", { 0.0f, 1.0f }, 0.2f);
    addFloat ("Filter", "envamount", "Env Amount", { -1.0f, 1.0f }, 0.4f);
    addFloat ("Filter", "keytrack", "Key Track", { 0.0f, 1.0f }, 0.5f);
    addFloat ("Filter Envelope", "fa", "Attack", time (5.0f), 0.005f, " s");
    addFloat ("Filter Envelope", "fd", "Decay", time (5.0f), 0.4f, " s");
    addFloat ("Filter Envelope", "fs", "Sustain", { 0.0f, 1.0f }, 0.3f);
    addFloat ("Filter Envelope", "fr", "Release", time (8.0f), 0.4f, " s");
    addFloat ("Amp Envelope", "aa", "Attack", time (5.0f), 0.005f, " s");
    addFloat ("Amp Envelope", "ad", "Decay", time (5.0f), 0.3f, " s");
    addFloat ("Amp Envelope", "as", "Sustain", { 0.0f, 1.0f }, 0.8f);
    addFloat ("Amp Envelope", "ar", "Release", time (8.0f), 0.3f, " s");
    addFloat ("Output", "glide", "Glide", time (2.0f), 0.001f, " s");
    addFloat ("Output", "volume", "Volume", { -48.0f, 6.0f }, -9.0f, " dB");
}

void KalibreSynth::fillInPluginDescription (PluginDescription& d) const
{
    d.name = "Kalibre Synth";
    d.manufacturerName = "Kalibre";
    d.pluginFormatName = "Kalibre";
    d.fileOrIdentifier = "synth";
    d.isInstrument = true;
    d.numInputChannels = 0;
    d.numOutputChannels = 2;
}

void KalibreSynth::prepareToPlay (double r, int)
{
    rate = r;
    for (auto& v : voices) { v.amp.setSampleRate (r); v.filter.setSampleRate (r); v.amp.reset(); v.filter.reset(); v.note = -1; }
}

void KalibreSynth::noteOn (int note, float velocity)
{
    // A free voice, or the oldest.
    Voice* chosen = nullptr;
    for (auto& v : voices) if (! v.amp.isActive()) { chosen = &v; break; }
    if (! chosen)
    {
        chosen = &voices[0];
        for (auto& v : voices) if (v.age < chosen->age) chosen = &v;
    }
    auto& v = *chosen;
    const double hz = MidiMessage::getMidiNoteInHertz (note);
    // Glide from the last note played.
    v.freq = lastNote >= 0 && value (Glide) > 0.005f ? MidiMessage::getMidiNoteInHertz (lastNote) : hz;
    v.target = hz;
    v.note = note;
    v.velocity = velocity;
    v.age = ++counter;
    if (! v.amp.isActive()) { v.ic1 = v.ic2 = v.ic1r = v.ic2r = 0; v.phase1 = v.phase2 = 0; }
    v.amp.setParameters (adsr (value (AA), value (AD), value (AS), value (AR)));
    v.filter.setParameters (adsr (value (FA), value (FD), value (FS), value (FR)));
    v.amp.noteOn();
    v.filter.noteOn();
    lastNote = note;
}

void KalibreSynth::noteOff (int note)
{
    for (auto& v : voices)
        if (v.note == note) { v.amp.noteOff(); v.filter.noteOff(); v.note = -1; }
}

void KalibreSynth::render (AudioBuffer<float>& out, int start, int n)
{
    const int w1 = (int) value (Wave1), w2 = (int) value (Wave2);
    const double ratio2 = std::pow (2.0, (value (Detune) + value (Fine) / 100.0) / 12.0);
    const float mix = value (Mix), noise = value (Noise), cutoff = value (Cutoff), res = value (Resonance);
    const float envAmt = value (EnvAmount), keyTrack = value (KeyTrack);
    const double glideCoeff = std::exp (-1.0 / (jmax (0.001, (double) value (Glide)) * rate));
    const float gain = Decibels::decibelsToGain (value (Volume));
    const double bendRatio = std::pow (2.0, bend * 2.0 / 12.0);
    for (auto& v : voices)
    {
        if (! v.amp.isActive()) continue;
        auto* l = out.getWritePointer (0, start);
        auto* r = out.getWritePointer (1, start);
        for (int i = 0; i < n; ++i)
        {
            v.freq = v.target + (v.freq - v.target) * glideCoeff;
            const double vib = 1.0 + wheel * 0.01 * std::sin (vibrato);
            const double f = v.freq * bendRatio * vib;
            const double dt1 = f / rate, dt2 = f * ratio2 / rate;
            const double s = oscillator (w1, v.phase1, dt1) * (1.0 - mix) + oscillator (w2, v.phase2, dt2) * mix
                           + (v.noise.nextFloat() * 2.0f - 1.0f) * noise;
            v.phase1 += dt1; if (v.phase1 >= 1.0) v.phase1 -= 1.0;
            v.phase2 += dt2; if (v.phase2 >= 1.0) v.phase2 -= 1.0;
            const float fenv = v.filter.getNextSample();
            const float tracked = cutoff * std::pow ((float) (f / 261.63), keyTrack);
            const float fc = tracked * std::pow (2.0f, envAmt * 6.0f * fenv * (0.5f + 0.5f * v.velocity));
            const float x = lowPass ((float) s, fc, res, rate, v.ic1, v.ic2);
            const float a = v.amp.getNextSample() * (0.3f + 0.7f * v.velocity) * gain;
            l[i] += x * a;
            r[i] += x * a;
        }
    }
    vibrato += MathConstants<double>::twoPi * 5.5 * n / rate;
    if (vibrato > 1000.0) vibrato = std::fmod (vibrato, MathConstants<double>::twoPi);
}

void KalibreSynth::processBlock (AudioBuffer<float>& audio, MidiBuffer& midi)
{
    audio.clear();
    int at = 0;
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        const int pos = jlimit (0, audio.getNumSamples(), m.samplePosition);
        if (pos > at) { render (audio, at, pos - at); at = pos; }
        if (msg.isNoteOn()) noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff()) noteOff (msg.getNoteNumber());
        else if (msg.isPitchWheel()) bend = (float) (msg.getPitchWheelValue() - 8192) / 8192.0f;
        else if (msg.isControllerOfType (1)) wheel = (float) msg.getControllerValue() / 127.0f;
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) for (auto& v : voices) { v.amp.reset(); v.filter.reset(); v.note = -1; }
    }
    if (at < audio.getNumSamples()) render (audio, at, audio.getNumSamples() - at);
    midi.clear();
}

AudioProcessorEditor* KalibreSynth::createEditor() { return new SynthEditor (*this); }

//==============================================================================
KalibreSampler::KalibreSampler()
{
    auto time = [] (float top) { return NormalisableRange<float> (0.001f, top, 0.0f, 0.3f); };
    addFloat ("Pitch", "root", "Root Note", { 0.0f, 127.0f, 1.0f }, 60.0f);
    addFloat ("Pitch", "tune", "Tune", { -24.0f, 24.0f, 0.01f }, 0.0f, " st");
    addFloat ("Sample", "start", "Start", { 0.0f, 1.0f }, 0.0f);
    addFloat ("Sample", "end", "End", { 0.0f, 1.0f }, 1.0f);
    addChoice ("Sample", "loop", "Loop", { "Off", "On" }, 0);
    addFloat ("Sample", "loopstart", "Loop Start", { 0.0f, 1.0f }, 0.0f);
    addFloat ("Filter", "cutoff", "Cutoff", { 30.0f, 20000.0f, 0.0f, 0.25f }, 20000.0f, " Hz");
    addFloat ("Filter", "resonance", "Resonance", { 0.0f, 1.0f }, 0.0f);
    addFloat ("Filter", "velsense", "Velocity", { 0.0f, 1.0f }, 0.7f);
    addFloat ("Envelope", "a", "Attack", time (5.0f), 0.002f, " s");
    addFloat ("Envelope", "d", "Decay", time (5.0f), 0.5f, " s");
    addFloat ("Envelope", "s", "Sustain", { 0.0f, 1.0f }, 1.0f);
    addFloat ("Envelope", "r", "Release", time (8.0f), 0.2f, " s");
    addFloat ("Envelope", "volume", "Volume", { -48.0f, 6.0f }, -3.0f, " dB");
    // How it's played (0095): Keys, Slices (cut at its hits), Granular; and how fast.
    addChoice ("Play", "mode", "Mode", { "Keys", "Slices", "Granular" }, 0);
    NormalisableRange<float> speed (0.0f, 4.0f);
    speed.setSkewForCentre (1.0f);
    addFloat ("Play", "speed", "Speed", speed, 1.0f, "x");
    addFloat ("Play", "sensitivity", "Slice Sensitivity", { 0.0f, 1.0f }, 0.5f);
    addFloat ("Grains", "grainsize", "Grain Size", { 0.01f, 0.5f, 0.0f, 0.5f }, 0.08f, " s");
    addFloat ("Grains", "density", "Density", { 2.0f, 100.0f, 0.0f, 0.5f }, 24.0f, "/s");
    addFloat ("Grains", "spray", "Spray", { 0.0f, 1.0f }, 0.1f);
    addFloat ("Grains", "position", "Position", { 0.0f, 1.0f }, 0.0f);
    jassert ((int) params.size() == Count);
}

std::vector<int> KalibreSampler::slicePoints()
{
    const SpinLock::ScopedLockType lock (swapLock);
    std::vector<int> out;
    const int len = data.getNumSamples();
    if (len == 0) return out;
    const double start = value (Start) * len, end = jmax (start + 1.0, (double) value (End) * len);
    double from = 0, to = 0;
    for (int k = 0; sliceBounds (k, start, end, from, to); ++k) out.push_back ((int) from);
    return out;
}

bool KalibreSampler::sliceBounds (int index, double start, double end, double& from, double& to) const
{
    if (index < 0) return false;
    const double sens = value (Sensitivity);
    const int gap = audiotools::hitGapFrames (fileRate);
    const double nearStart = fileRate * 0.01;   // a hit at the very start doesn't make an empty slice
    int last = -gap - 1, k = 0;
    from = start;
    for (const auto& h : candidates)
    {
        if (! audiotools::isHit (h, sens, last, gap)) continue;
        last = h.frame;
        if (h.at <= start + nearStart) continue;
        if (h.at >= end) break;
        if (k == index) { to = h.at; return true; }
        from = h.at;
        ++k;
    }
    if (k != index) return false;
    to = end;
    return to - from >= 1.0;
}

void KalibreSampler::fillInPluginDescription (PluginDescription& d) const
{
    d.name = "Kalibre Sampler";
    d.manufacturerName = "Kalibre";
    d.pluginFormatName = "Kalibre";
    d.fileOrIdentifier = "sampler";
    d.isInstrument = true;
    d.numInputChannels = 0;
    d.numOutputChannels = 2;
}

void KalibreSampler::prepareToPlay (double r, int)
{
    rate = r;
    for (auto& v : voices) { v.env.setSampleRate (r); v.env.reset(); v.note = -1; }
}

bool KalibreSampler::load (const File& f, String& error)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> reader (fm.createReaderFor (f));
    if (! reader) { error = "It isn't an audio file Kalibre can read."; return false; }
    if (reader->lengthInSamples > (int64) reader->sampleRate * 600) { error = "It's longer than ten minutes."; return false; }
    AudioBuffer<float> b (2, (int) reader->lengthInSamples);
    reader->read (&b, 0, b.getNumSamples(), 0, true, true);
    if (reader->numChannels == 1) b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
    // Its hits, found once here, so a slice sensitivity can be chosen while it plays (0095).
    auto hits = audiotools::hitCandidates (b.getArrayOfReadPointers(), 2, b.getNumSamples(), reader->sampleRate);
    {
        const SpinLock::ScopedLockType lock (swapLock);
        data = std::move (b);
        candidates = std::move (hits);
        fileRate = reader->sampleRate;
        sampleFile = f;
        for (auto& v : voices) { v.env.reset(); v.note = -1; }
    }
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    if (onLoaded) onLoaded();
    return true;
}

void KalibreSampler::writeExtra (XmlElement& x) { x.setAttribute ("sample", sampleFile.getFullPathName()); }

void KalibreSampler::readExtra (const XmlElement& x)
{
    const File f (x.getStringAttribute ("sample"));
    String error;
    if (f.existsAsFile() && f != sampleFile) load (f, error);
}

void KalibreSampler::processBlock (AudioBuffer<float>& audio, MidiBuffer& midi)
{
    audio.clear();
    const SpinLock::ScopedTryLockType lock (swapLock);
    if (! lock.isLocked() || data.getNumSamples() == 0) { midi.clear(); return; }
    const int len = data.getNumSamples();
    const double start = value (Start) * len, end = jmax (start + 1.0, (double) value (End) * len);
    const bool looping = value (Loop) > 0.5f;
    const double loopStart = jlimit (start, end - 1.0, (double) value (LoopStart) * len);
    const float cutoff = value (Cutoff), res = value (Resonance), sense = value (VelSense);
    const float gain = Decibels::decibelsToGain (value (Volume));
    const double base = fileRate / rate;
    const float root = value (Root), tune = value (Tune);
    const auto how = mode();
    const double speed = value (Speed);
    const double tapeSpeed = jmax (0.05, speed);   // Keys and Slices: faster is higher, as tape
    // Granular (0095): grains of `grainLength` output samples, one every `grainEvery`.
    const int grainLength = jmax (16, (int) (value (GrainSize) * rate));
    const double grainEvery = rate / jmax (1.0f, value (Density));
    const double spray = value (Spray);
    const float grainGain = 1.0f / std::sqrt (jmax (1.0f, (float) grainLength / (float) grainEvery * 0.5f));
    const double fadeSamples = fileRate * 0.003;   // a slice's last 3 ms fade, so its cut never clicks

    auto read = [&] (int ch, double pos)
    {
        if (pos < 0 || pos >= len - 1) return 0.0f;
        const int i0 = (int) pos;
        const float frac = (float) (pos - i0);
        return data.getSample (ch, i0) + (data.getSample (ch, i0 + 1) - data.getSample (ch, i0)) * frac;
    };

    auto renderTo = [&] (int from, int to)
    {
        const double bendStep = std::pow (2.0, bend * 2.0 / 12.0);
        for (auto& v : voices)
        {
            if (! v.env.isActive()) continue;
            for (int i = from; i < to; ++i)
            {
                float sl = 0, sr = 0;
                if (how == PlayMode::Granular)
                {
                    // The reading head moves at Speed; each grain plays at the key's pitch.
                    if (--v.untilGrain <= 0)
                    {
                        v.untilGrain += grainEvery;
                        for (auto& gr : v.grains)
                            if (! gr.on)
                            {
                                gr.on = true;
                                gr.age = 0;
                                gr.length = grainLength;
                                gr.step = v.step * bendStep;
                                const double reach = spray * grainLength * gr.step;
                                gr.pos = jlimit (start, end - 1.0, v.head + (v.random.nextDouble() * 2.0 - 1.0) * reach);
                                break;
                            }
                    }
                    for (auto& gr : v.grains)
                    {
                        if (! gr.on) continue;
                        const float w = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * (float) gr.age / (float) gr.length);
                        sl += read (0, gr.pos) * w;
                        sr += read (1, gr.pos) * w;
                        gr.pos += gr.step;
                        if (++gr.age >= gr.length || gr.pos >= end) gr.on = false;
                    }
                    sl *= grainGain;
                    sr *= grainGain;
                    v.head += base * speed;
                    if (v.head >= end)
                    {
                        if (looping) v.head = loopStart + std::fmod (v.head - end, jmax (1.0, end - loopStart));
                        else
                        {
                            v.head = end - 1.0;
                            if (! v.released) { v.env.noteOff(); v.released = true; }
                        }
                    }
                }
                else
                {
                    const double stop = how == PlayMode::Slices ? v.sliceEnd : end;
                    if (v.pos >= stop)
                    {
                        if (looping && how == PlayMode::Keys) v.pos = loopStart + std::fmod (v.pos - end, jmax (1.0, end - loopStart));
                        else { v.env.reset(); break; }
                    }
                    sl = read (0, v.pos);
                    sr = read (1, v.pos);
                    if (how == PlayMode::Slices && stop - v.pos < fadeSamples)
                    {
                        const float f = (float) jmax (0.0, (stop - v.pos) / fadeSamples);
                        sl *= f;
                        sr *= f;
                    }
                    v.pos += v.step * bendStep;
                }
                if (cutoff < 19999.0f)
                {
                    sl = lowPass (sl, cutoff, res, rate, v.ic1, v.ic2);
                    sr = lowPass (sr, cutoff, res, rate, v.ic1r, v.ic2r);
                }
                const float a = v.env.getNextSample() * (1.0f - sense + sense * v.velocity) * gain;
                audio.addSample (0, i, sl * a);
                audio.addSample (1, i, sr * a);
            }
        }
    };

    int at = 0;
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        const int pos = jlimit (0, audio.getNumSamples(), m.samplePosition);
        if (pos > at) { renderTo (at, pos); at = pos; }
        if (msg.isNoteOn())
        {
            // Slices: the root plays the first slice, each key up the next; past the last, nothing.
            double sliceFrom = start, sliceTo = end;
            if (how == PlayMode::Slices && ! sliceBounds (msg.getNoteNumber() - roundToInt (root), start, end, sliceFrom, sliceTo)) continue;
            Voice* chosen = nullptr;
            for (auto& v : voices) if (! v.env.isActive()) { chosen = &v; break; }
            if (! chosen) chosen = &voices[(size_t) (msg.getNoteNumber() % (int) voices.size())];
            chosen->note = msg.getNoteNumber();
            chosen->velocity = msg.getFloatVelocity();
            chosen->released = false;
            chosen->pos = sliceFrom;
            chosen->sliceEnd = sliceTo;
            const double pitch = how == PlayMode::Slices ? tune : msg.getNoteNumber() - root + tune;
            chosen->step = base * std::pow (2.0, pitch / 12.0) * (how == PlayMode::Granular ? 1.0 : tapeSpeed);
            chosen->head = start + (double) value (Position) * (end - start);
            chosen->untilGrain = 0;
            for (auto& gr : chosen->grains) gr.on = false;
            chosen->ic1 = chosen->ic2 = chosen->ic1r = chosen->ic2r = 0;
            chosen->env.setParameters (adsr (value (A), value (D), value (S), value (R)));
            chosen->env.noteOn();
        }
        else if (msg.isNoteOff())
        {
            for (auto& v : voices) if (v.note == msg.getNoteNumber()) { v.env.noteOff(); v.released = true; v.note = -1; }
        }
        else if (msg.isPitchWheel()) bend = (float) (msg.getPitchWheelValue() - 8192) / 8192.0f;
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) for (auto& v : voices) { v.env.reset(); v.note = -1; }
    }
    if (at < audio.getNumSamples()) renderTo (at, audio.getNumSamples());
    midi.clear();
}

AudioProcessorEditor* KalibreSampler::createEditor() { return new SamplerEditor (*this); }

} // namespace kal

//==============================================================================
// Trombolese (decision 0093).
namespace kal
{
struct TromboleseVoice
{
    TromboleseVoice (double rate) : geometry (trombolese::Geometry::forRate (rate)), voice (geometry, rate) {}
    trombolese::Geometry geometry;
    trombolese::Voice voice;
};

namespace
{
class TromboleseEditor : public AudioProcessorEditor
{
public:
    explicit TromboleseEditor (TromboleseInstrument& p) : AudioProcessorEditor (p), knobs (p)
    {
        addAndMakeVisible (knobs);
        setResizable (true, false);
        setResizeLimits (300, 160, 3000, 1600);
        setSize (620, 220);
    }
    void resized() override { knobs.setBounds (getLocalBounds()); }

private:
    KnobPanel knobs;
};
} // namespace

TromboleseInstrument::TromboleseInstrument()
{
    auto time = [] (float top) { return NormalisableRange<float> (0.005f, top, 0.0f, 0.4f); };
    addFloat ("Instrument", "bore", "Bore (Cylinder to Cone)", { 0.0f, 1.0f }, 0.0f);
    addFloat ("Instrument", "reed", "Reed (Lips to Double Reed)", { 0.0f, 1.0f }, 0.0f);
    addFloat ("Breath", "breath", "Breath", { 2500.0f, 6000.0f }, 4200.0f, " Pa");
    addFloat ("Breath", "attack", "Attack", time (1.0f), 0.05f, " s");
    addFloat ("Breath", "release", "Release", time (2.0f), 0.12f, " s");
    addFloat ("Output", "glide", "Slide Time", time (1.0f), 0.08f, " s");
    addFloat ("Output", "volume", "Volume", { -48.0f, 6.0f }, -6.0f, " dB");
}

TromboleseInstrument::~TromboleseInstrument() = default;

void TromboleseInstrument::fillInPluginDescription (PluginDescription& d) const
{
    d.name = "Trombolese";
    d.manufacturerName = "Kalibre";
    d.pluginFormatName = "Kalibre";
    d.fileOrIdentifier = "trombolese";
    d.isInstrument = true;
    d.numInputChannels = 0;
    d.numOutputChannels = 2;
}

void TromboleseInstrument::prepareToPlay (double r, int)
{
    rate = r;
    voice = std::make_unique<TromboleseVoice> (r);   // the ladder made for this rate, here, not while playing
    block.assign (trombolese::Voice::blockSize, 0.0);
    heldCount = 0;
    note = -1;
    breath = 0;
}

void TromboleseInstrument::start (int n, float vel, bool legato)
{
    const double alpha = jlimit (0.0, 1.0, (double) value (Bore) + wheel);
    const auto s = trombolese::settingFor (n, alpha);
    const double f = 440.0 * std::pow (2.0, (n - 69) / 12.0);
    lipTarget = f * s.lipRatio;
    slideTarget = s.slide;
    levelTarget = s.level > 1e-6 ? 0.12 / s.level : 1.0;
    // A fresh note starts where it's going; one played over another slides there.
    if (! legato || note < 0) { lip = lipTarget; slide = slideTarget; level = levelTarget; velocity = vel; }
    note = n;
}

void TromboleseInstrument::render (AudioBuffer<float>& audio, int startSample, int n)
{
    if (! voice) return;
    const int bs = trombolese::Voice::blockSize;
    const double alpha = jlimit (0.0, 1.0, (double) value (Bore) + wheel);
    const double beta = value (Reed);
    const double gain = Decibels::decibelsToGain ((double) value (Volume));
    for (int done = 0; done < n;)
    {
        const int count = jmin (bs, n - done);
        const double dt = count / rate;
        // The breath: rises while a note is held, falls when let go.
        if (heldCount > 0) breath = jmin (1.0, breath + dt / jmax (0.005, (double) value (Attack)));
        else breath = jmax (0.0, breath - dt / jmax (0.005, (double) value (Release)));
        // Sliding to the next note's position and embouchure.
        const double glide = jmin (1.0, dt / jmax (0.005, (double) value (Glide)));
        lip += (lipTarget - lip) * glide;
        slide += (slideTarget - slide) * glide;
        level += (levelTarget - level) * glide;
        trombolese::Controls c;
        c.pressure = value (Breath) * breath * (0.8 + 0.2 * velocity);
        c.lipFrequency = jmax (20.0, lip);
        c.alpha = alpha;
        c.beta = beta;
        // Pitch bend moves the slide: two semitones, as length goes.
        const double length = voice->geometry.boreLengthAt (alpha) + 0.72;
        c.slide = (length + slide) * std::pow (2.0, -2.0 * bend / 12.0) - length;
        if (breath <= 0 && heldCount == 0)
        {
            std::fill (block.begin(), block.begin() + count, 0.0);
            voice->voice.reset();   // silent: the next note starts from rest
        }
        else voice->voice.block (block.data(), count, c);
        for (int i = 0; i < count; ++i)
        {
            const float x = (float) (block[(size_t) i] * level * gain);
            for (int ch = 0; ch < audio.getNumChannels(); ++ch) audio.addSample (ch, startSample + done + i, x);
        }
        done += count;
    }
}

void TromboleseInstrument::processBlock (AudioBuffer<float>& audio, MidiBuffer& midi)
{
    audio.clear();
    int at = 0;
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        const int pos = jlimit (0, audio.getNumSamples(), m.samplePosition);
        if (pos > at) { render (audio, at, pos - at); at = pos; }
        if (msg.isNoteOn())
        {
            const bool legato = heldCount > 0;
            if (heldCount < (int) held.size()) held[(size_t) heldCount++] = msg.getNoteNumber();
            start (msg.getNoteNumber(), msg.getFloatVelocity(), legato);
        }
        else if (msg.isNoteOff())
        {
            for (int i = 0; i < heldCount; ++i)
                if (held[(size_t) i] == msg.getNoteNumber()) { held[(size_t) i] = held[(size_t) --heldCount]; break; }
            // Back to the note still held, the last played.
            if (heldCount > 0 && msg.getNoteNumber() == note) start (held[(size_t) heldCount - 1], velocity, true);
        }
        else if (msg.isPitchWheel()) bend = (float) (msg.getPitchWheelValue() - 8192) / 8192.0f;
        else if (msg.isControllerOfType (1)) wheel = (float) msg.getControllerValue() / 127.0f;
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) { heldCount = 0; breath = 0; }
    }
    if (at < audio.getNumSamples()) render (audio, at, audio.getNumSamples() - at);
    midi.clear();
}

AudioProcessorEditor* TromboleseInstrument::createEditor() { return new TromboleseEditor (*this); }
} // namespace kal
