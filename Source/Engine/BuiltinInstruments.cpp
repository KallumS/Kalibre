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
    // Another theme (0132): the knobs' names take its colour.
    void lookAndFeelChanged() override { for (auto* l : labels) l->setColour (Label::textColourId, theme::textSecondary); }

    explicit KnobPanel (BuiltinInstrument& p)
    {
        for (auto& g : p.groups)
        {
            auto& group = groups.emplace_back();
            group.name = g.name;
            for (auto* prm : g.params)
            {
                knobParams.push_back (prm);
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

    // Which knobs are shown (Kalibre Sampler's: only its mode's, 0117); all when not set.
    std::function<bool (const RangedAudioParameter&)> shows;
    bool shown (int knob) const { return ! shows || shows (*knobParams[(size_t) knob]); }
    void refresh()
    {
        for (int i = 0; i < knobs.size(); ++i) { knobs[i]->setVisible (shown (i)); labels[i]->setVisible (shown (i)); }
        resized();
        repaint();
    }
    int visibleIn (const std::vector<int>& indices) const
    {
        int n = 0;
        for (int i : indices) n += shown (i) ? 1 : 0;
        return n;
    }

    void paint (Graphics& g) override
    {
        g.fillAll (theme::sunken);
        g.setFont (theme::font (theme::smallPoints, true));
        for (auto& gr : groups)
        {
            if (gr.box.isEmpty()) continue;
            g.setColour (theme::lane);
            g.fillRoundedRectangle (gr.box.toFloat(), 5.0f);
            g.setColour (theme::text);
            // A copy: cutting the heading off the box itself moved it down every time it was drawn.
            auto box = gr.box;
            g.drawText (gr.name, box.removeFromTop (18).reduced (8, 0), Justification::centredLeft, false);
        }
    }

    void resized() override
    {
        // Each group a box across; its knobs in two rows.
        auto r = getLocalBounds().reduced (6);
        int total = 0, shownGroups = 0;
        for (auto& gr : groups)
            if (const int n = visibleIn (gr.indices); n > 0) { total += (n + 1) / 2; ++shownGroups; }
        const int unit = total > 0 ? (r.getWidth() - 6 * jmax (0, shownGroups - 1)) / total : 0;
        for (auto& gr : groups)
        {
            const int n = visibleIn (gr.indices);
            if (n == 0) { gr.box = {}; continue; }
            const int cols = (n + 1) / 2;
            gr.box = r.removeFromLeft (unit * cols);
            r.removeFromLeft (6);
            auto inner = gr.box.withTrimmedTop (18).reduced (4);
            const int rowH = inner.getHeight() / 2;
            int k = 0;
            for (int idx : gr.indices)
            {
                if (! shown (idx)) continue;
                const int col = k % cols, row = k / cols;
                ++k;
                Rectangle<int> cell (inner.getX() + col * unit, inner.getY() + row * rowH, unit, rowH);
                labels[idx]->setBounds (cell.removeFromTop (14));
                knobs[idx]->setBounds (cell.reduced (2));
            }
        }
    }

private:
    struct G { String name; std::vector<int> indices; Rectangle<int> box; };
    std::vector<G> groups;
    std::vector<RangedAudioParameter*> knobParams;
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
        // Keys, Slices, Granular along the top (0117): each shows its own knobs, and only those.
        const char* tips[] = { "Keys: the sample played across the keyboard from its Root Note, faster and higher up the keys.",
                               "Slices: the sample cut at its hits, a key each from the Root Note up: play a drum loop's hits from the keys.",
                               "Granular: the sample played as many short grains, so its speed and its pitch are apart." };
        for (int m = 0; m < 3; ++m)
        {
            auto* b = modes.add (new theme::HouseButton (modeNames[m], tips[m]));
            b->onClick = [this, m] { chooseMode (m); };
            addAndMakeVisible (b);
        }
        knobs.shows = [this] (const RangedAudioParameter& prm) { return showsKnob (prm.getParameterID()); };
        showMode();
        addAndMakeVisible (loadButton);
        addAndMakeVisible (randomButton);
        randomButton.onClick = [this]
        {
            String said;
            const bool ok = proc.loadRandom (said);
            if (KalibreSampler::say) KalibreSampler::say (said, ! ok);
            repaint();
        };
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
        startTimerHz (30);   // the preview's line moves smoothly
    }
    ~SamplerEditor() override { proc.onLoaded = nullptr; }

    // The marks follow the knobs: drawn again when one that moves them changes.
    void timerCallback() override
    {
        String now;
        for (int i : { (int) KalibreSampler::Start, (int) KalibreSampler::End, (int) KalibreSampler::Loop, (int) KalibreSampler::LoopStart,
                       (int) KalibreSampler::Mode, (int) KalibreSampler::Sensitivity, (int) KalibreSampler::Position })
            now << proc.value (i) << ",";
        if (now != shown) { shown = now; slices = proc.mode() == KalibreSampler::PlayMode::Slices ? proc.slicePoints() : std::vector<int>(); showMode(); repaint(); }
        // The preview's line, while it plays.
        const float at = proc.auditionPosition();
        if (at != previewShown) { previewShown = at; repaint (waveArea()); }
    }

    static constexpr const char* modeNames[3] { "Keys", "Slices", "Granular" };

    // A knob shown in this mode: Mode itself never (the buttons are it); Loop in Keys and
    // Granular; Slice Sensitivity in Slices; the grains in Granular; the rest always.
    bool showsKnob (const String& id) const
    {
        const auto m = proc.mode();
        if (id == "mode") return false;
        if (id == "loop" || id == "loopstart") return m != KalibreSampler::PlayMode::Slices;
        if (id == "sensitivity") return m == KalibreSampler::PlayMode::Slices;
        if (id == "grainsize" || id == "density" || id == "spray" || id == "position") return m == KalibreSampler::PlayMode::Granular;
        return true;
    }

    void chooseMode (int m)
    {
        if (auto* prm = dynamic_cast<RangedAudioParameter*> (proc.getParameters()[(int) KalibreSampler::Mode]))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) m));
            prm->endChangeGesture();
        }
        showMode();
    }

    void showMode()
    {
        const int now = (int) proc.mode();
        for (int m = 0; m < 3; ++m) modes[m]->setToggleState (m == now, dontSendNotification);
        if (now != modeShown) { modeShown = now; knobs.refresh(); }
    }

    // A click on the waveform plays the sample from there; another click cuts it off (0117).
    void mouseDown (const MouseEvent& e) override
    {
        const auto w = waveArea();
        if (! w.contains (e.getPosition()) || proc.sample().getNumSamples() == 0 || e.mods.isPopupMenu()) return;
        proc.audition ((float) (e.x - w.getX()) / (float) jmax (1, w.getWidth()));
    }

    void loadFile (const File& f)
    {
        String error;
        bool loaded = false;
        proc.perform ("Load Sample", [&] { loaded = proc.loadInTune (f, error); });
        if (! loaded)
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
            g.drawFittedText ("Drop a sample here, from the browser or the Finder, or click Load Sample or Random Sample.", w, Justification::centred, 2);
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
        if (proc.value (KalibreSampler::Loop) > 0.5f && how != KalibreSampler::PlayMode::Slices) mark (proc.value (KalibreSampler::LoopStart), theme::edge);
        // Where the preview is (clicked on the waveform), in the accent: it's playing.
        if (const float at = proc.auditionPosition(); at >= 0) mark (at, theme::accent);
        g.setColour (theme::text);
        const String modeName = how == KalibreSampler::PlayMode::Slices ? "Slices: " + String ((int) slices.size()) + ", a key each from the root"
                              : how == KalibreSampler::PlayMode::Granular ? String ("Granular: the grains start at Position")
                              : String ("Keys: across the keyboard from the root");
        g.drawText (proc.file().getFileName() + "   " + modeName + "   (click to hear it from there)", w.reduced (6, 4), Justification::topLeft, true);
    }

    // The top row: the modes on the left, Load Sample and Random Sample on the right;
    // the waveform under it, as wide as the panel.
    Rectangle<int> topRow() const { return getLocalBounds().removeFromTop (waveHeight()).reduced (6).removeFromTop (theme::px (24)); }
    Rectangle<int> waveArea() const
    {
        return getLocalBounds().removeFromTop (waveHeight()).reduced (6).withTrimmedTop (theme::px (24) + 6);
    }

    // The waveform gives way first in a short panel, so the knobs stay big enough to turn
    // (the UAT's were dots): two fifths, but no less than 90 (the buttons' row and a strip
    // of waveform) and leaving the knobs 170.
    int waveHeight() const { return jlimit (90, jmax (90, getHeight() * 2 / 5), getHeight() - 170); }

    void resized() override
    {
        auto row = topRow();
        for (auto* b : modes) { b->setBounds (row.removeFromLeft (b->idealWidth (theme::smallControl))); row.removeFromLeft (6); }
        randomButton.setBounds (row.removeFromRight (randomButton.idealWidth (theme::smallControl)));
        row.removeFromRight (6);
        loadButton.setBounds (row.removeFromRight (loadButton.idealWidth (theme::smallControl)));
        knobs.setBounds (getLocalBounds().withTrimmedTop (waveHeight()));
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
    OwnedArray<theme::HouseButton> modes;
    int modeShown = -1;
    float previewShown = -1;
    theme::HouseButton loadButton { ui::u8 ("Load Sample\xe2\x80\xa6"), "Choose the sample to play." };
    theme::HouseButton randomButton { "Random Sample", "Any sound from your Sounds folders: a loop, a one-shot, a note, a texture. "
                                                       "A melodic one plays in tune: its Root Note is set to the note it is. Click again for another." };
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
        return ui::number (v, places) + suffix;
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

//==============================================================================
// The factory presets: what each sets, in the knobs' own units; the rest at their defaults.
namespace
{
struct SynthPreset { const char* name; std::vector<std::pair<int, float>> values; };
const std::vector<SynthPreset>& synthPresets()
{
    using S = KalibreSynth;
    enum { Saw, Square, Triangle, Sine };
    static const std::vector<SynthPreset> presets {
        { "Init", {} },
        { "Warm Bass", { { S::Wave1, Saw }, { S::Wave2, Square }, { S::Detune, -12 }, { S::Fine, 3 }, { S::Mix, 0.4f }, { S::Cutoff, 600 },
                         { S::Resonance, 0.25f }, { S::EnvAmount, 0.35f }, { S::KeyTrack, 0.3f }, { S::FD, 0.25f }, { S::FS, 0.2f }, { S::FR, 0.2f },
                         { S::AA, 0.003f }, { S::AD, 0.3f }, { S::AS, 0.9f }, { S::AR, 0.12f }, { S::Volume, -8 } } },
        { "Sub Bass", { { S::Wave1, Sine }, { S::Wave2, Triangle }, { S::Detune, -12 }, { S::Fine, 0 }, { S::Mix, 0.3f }, { S::Cutoff, 300 },
                        { S::Resonance, 0.05f }, { S::EnvAmount, 0.1f }, { S::KeyTrack, 0.2f }, { S::AA, 0.005f }, { S::AD, 0.2f }, { S::AS, 1.0f },
                        { S::AR, 0.15f }, { S::Volume, -12 } } },
        { "Acid Bass", { { S::Wave1, Saw }, { S::Wave2, Saw }, { S::Fine, 0 }, { S::Mix, 0.0f }, { S::Cutoff, 300 }, { S::Resonance, 0.8f },
                         { S::EnvAmount, 0.8f }, { S::KeyTrack, 0.4f }, { S::FA, 0.002f }, { S::FD, 0.18f }, { S::FS, 0.05f }, { S::FR, 0.1f },
                         { S::AA, 0.002f }, { S::AD, 0.2f }, { S::AS, 0.7f }, { S::AR, 0.08f }, { S::Glide, 0.06f }, { S::Volume, -12 } } },
        { "Soft Pad", { { S::Wave1, Saw }, { S::Wave2, Saw }, { S::Fine, 12 }, { S::Mix, 0.5f }, { S::Noise, 0.03f }, { S::Cutoff, 1400 },
                        { S::Resonance, 0.15f }, { S::EnvAmount, 0.2f }, { S::FA, 0.8f }, { S::FD, 1.5f }, { S::FS, 0.5f }, { S::FR, 1.5f },
                        { S::AA, 0.9f }, { S::AD, 1.0f }, { S::AS, 0.8f }, { S::AR, 1.8f }, { S::Volume, -14 } } },
        { "Bright Lead", { { S::Wave1, Saw }, { S::Wave2, Square }, { S::Detune, 12 }, { S::Fine, 5 }, { S::Mix, 0.35f }, { S::Cutoff, 4500 },
                           { S::Resonance, 0.3f }, { S::EnvAmount, 0.3f }, { S::KeyTrack, 0.6f }, { S::FD, 0.3f }, { S::FS, 0.5f }, { S::FR, 0.3f },
                           { S::AA, 0.005f }, { S::AD, 0.2f }, { S::AS, 0.85f }, { S::AR, 0.2f }, { S::Glide, 0.04f }, { S::Volume, -12 } } },
        { "Pluck", { { S::Wave1, Square }, { S::Wave2, Saw }, { S::Detune, 12 }, { S::Fine, 4 }, { S::Mix, 0.3f }, { S::Cutoff, 900 },
                     { S::Resonance, 0.3f }, { S::EnvAmount, 0.7f }, { S::KeyTrack, 0.6f }, { S::FA, 0.001f }, { S::FD, 0.25f }, { S::FS, 0.0f },
                     { S::FR, 0.25f }, { S::AA, 0.001f }, { S::AD, 0.45f }, { S::AS, 0.0f }, { S::AR, 0.3f }, { S::Volume, -9 } } },
        { "Brass", { { S::Wave1, Saw }, { S::Wave2, Saw }, { S::Fine, 8 }, { S::Mix, 0.5f }, { S::Cutoff, 1100 }, { S::Resonance, 0.1f },
                     { S::EnvAmount, 0.55f }, { S::FA, 0.06f }, { S::FD, 0.5f }, { S::FS, 0.5f }, { S::FR, 0.3f }, { S::AA, 0.04f },
                     { S::AD, 0.3f }, { S::AS, 0.85f }, { S::AR, 0.2f }, { S::Volume, -12 } } },
        { "Keys", { { S::Wave1, Triangle }, { S::Wave2, Square }, { S::Detune, 12 }, { S::Fine, 0 }, { S::Mix, 0.25f }, { S::Cutoff, 2500 },
                    { S::Resonance, 0.1f }, { S::EnvAmount, 0.3f }, { S::KeyTrack, 0.7f }, { S::FA, 0.002f }, { S::FD, 0.6f }, { S::FS, 0.2f },
                    { S::FR, 0.4f }, { S::AA, 0.002f }, { S::AD, 1.2f }, { S::AS, 0.3f }, { S::AR, 0.5f }, { S::Volume, -10 } } },
        { "Strings", { { S::Wave1, Saw }, { S::Wave2, Saw }, { S::Fine, 10 }, { S::Mix, 0.5f }, { S::Cutoff, 3000 }, { S::Resonance, 0.05f },
                       { S::EnvAmount, 0.1f }, { S::FA, 0.3f }, { S::FD, 1.0f }, { S::FS, 0.7f }, { S::FR, 0.8f }, { S::AA, 0.35f },
                       { S::AD, 0.5f }, { S::AS, 0.9f }, { S::AR, 0.7f }, { S::Volume, -14 } } },
        { "Noise Sweep", { { S::Wave1, Sine }, { S::Wave2, Sine }, { S::Mix, 0.0f }, { S::Noise, 0.8f }, { S::Cutoff, 500 }, { S::Resonance, 0.6f },
                           { S::EnvAmount, 0.9f }, { S::FA, 2.0f }, { S::FD, 3.0f }, { S::FS, 0.2f }, { S::FR, 2.0f }, { S::AA, 0.5f },
                           { S::AD, 1.0f }, { S::AS, 0.8f }, { S::AR, 2.0f }, { S::Volume, -14 } } },
    };
    return presets;
}
} // namespace

int KalibreSynth::getNumPrograms() { return (int) synthPresets().size(); }
const String KalibreSynth::getProgramName (int i) { return i >= 0 && i < getNumPrograms() ? String (synthPresets()[(size_t) i].name) : String(); }

void KalibreSynth::setCurrentProgram (int i)
{
    if (i < 0 || i >= getNumPrograms()) return;
    program = i;
    for (auto* p : params) p->setValueNotifyingHost (p->getDefaultValue());
    for (auto& [index, v] : synthPresets()[(size_t) i].values)
        params[(size_t) index]->setValueNotifyingHost (params[(size_t) index]->convertTo0to1 (v));
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

void KalibreSynth::reset()
{
    for (auto& v : voices) { v.amp.reset(); v.filter.reset(); v.note = -1; v.ic1 = v.ic2 = v.ic1r = v.ic2r = 0; }
    bend = wheel = 0;
    vibrato = 0;
    lastNote = -1;
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
    seedGrains();
}

// Granular's spray is chance, but the same chance each time from the start, so
// an export comes out the same every time (0097). Each voice its own sequence.
void KalibreSampler::seedGrains()
{
    for (size_t i = 0; i < voices.size(); ++i) voices[i].random.setSeed ((int64) i + 1);
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

void KalibreSampler::unload()
{
    {
        const SpinLock::ScopedLockType lock (swapLock);
        data.setSize (2, 0);
        candidates.clear();
        sampleFile = File();
        for (auto& v : voices) { v.env.reset(); v.note = -1; }
    }
    if (onLoaded) onLoaded();
}

std::function<void (KalibreSampler&, const String&, std::function<void()>)> KalibreSampler::edit;
std::function<const library::Index*()> KalibreSampler::sounds;
std::function<void (const String&, bool)> KalibreSampler::say;

void KalibreSampler::perform (const String& label, std::function<void()> change)
{
    if (edit) edit (*this, label, std::move (change));
    else change();
}

int KalibreSampler::heardNote (const File& f) const
{
    const auto* index = sounds ? sounds() : nullptr;
    if (! index) return -1;
    const auto it = index->items.find (f.getFullPathName().toStdString());
    return it != index->items.end() && it->second.note >= 0 && it->second.note <= 127 ? it->second.note : -1;
}

bool KalibreSampler::loadInTune (const File& f, String& error)
{
    if (! load (f, error)) return false;
    // Played in tune: the key that plays it as recorded is the note the scan heard in it.
    if (const int note = heardNote (f); note >= 0)
        params[Root]->setValueNotifyingHost (params[Root]->convertTo0to1 ((float) note));
    return true;
}

bool KalibreSampler::loadRandom (String& said)
{
    const auto* index = sounds ? sounds() : nullptr;
    if (! index || index->items.empty())
    {
        said = "Random Sample chooses from your sounds: choose their folders in the browser's Sounds tab first.";
        return false;
    }
    // A file the scan read may since have gone: a few tries.
    const auto have = sampleFile.getFullPathName().toStdString();
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const auto path = library::randomSound (*index, (unsigned) Random::getSystemRandom().nextInt (1 << 30) + 1u, have);
        if (path.empty()) break;
        const File f (String::fromUTF8 (path.c_str()));
        if (! f.existsAsFile()) continue;
        const int note = heardNote (f);
        bool loaded = false;
        String error;
        perform ("Random Sample", [this, f, &loaded, &error] { loaded = loadInTune (f, error); });
        if (! loaded) continue;
        said = "Random Sample: " + f.getFileName()
             + (note >= 0 ? " (" + String (library::noteName (note, 0)) + ", its Root Note)" : String())
             + ". Click again for another; Undo for the last.";
        return true;
    }
    said = "No sound in your folders could be loaded: scan them again in the browser's Sounds tab.";
    return false;
}

void KalibreSampler::writeExtra (XmlElement& x) { x.setAttribute ("sample", sampleFile.getFullPathName()); }

void KalibreSampler::readExtra (const XmlElement& x)
{
    const File f (x.getStringAttribute ("sample"));
    String error;
    if (f.existsAsFile() && f != sampleFile) load (f, error);
    else if (x.getStringAttribute ("sample").isEmpty() && sampleFile != File()) unload();   // undone back to none
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
    renderPreview (audio, gain);
    midi.clear();
}

void KalibreSampler::audition (float fraction)
{
    auditionFrom = jlimit (0.0f, 1.0f, fraction);
    ++auditionAsked;
}

// The sample as it is, from where the waveform was clicked: no envelope, no filter,
// its own pitch; Volume only. A new click starts it again and fades the last out.
void KalibreSampler::renderPreview (AudioBuffer<float>& audio, float gain)
{
    const int len = data.getNumSamples();
    const double step = fileRate / rate;
    if (const int asked = auditionAsked.load(); asked != auditionSeen)
    {
        auditionSeen = asked;
        if (preview.pos >= 0) { cutOff = preview; cutOff.fadeStep = (float) (1.0 / (0.005 * rate)); }
        preview.pos = auditionFrom.load() * (double) jmax (0, len - 1);
        preview.fade = 0.0f;
        preview.fadeStep = -(float) (1.0 / (0.002 * rate));   // a 2 ms fade in, so it never clicks
    }
    auto play = [&] (Preview& p)
    {
        if (p.pos < 0) return;
        for (int i = 0; i < audio.getNumSamples(); ++i)
        {
            if (p.pos >= len - 1) { p.pos = -1; return; }
            // fadeStep < 0: fading in; > 0: fading out, gone at 0.
            if (p.fadeStep < 0) p.fade = jmin (1.0f, p.fade - p.fadeStep);
            else if (p.fadeStep > 0) { p.fade -= p.fadeStep; if (p.fade <= 0) { p.pos = -1; return; } }
            const int i0 = (int) p.pos;
            const float frac = (float) (p.pos - i0), g = gain * p.fade;
            for (int ch = 0; ch < jmin (2, audio.getNumChannels()); ++ch)
            {
                const int src = jmin (ch, data.getNumChannels() - 1);
                const float s = data.getSample (src, i0) + (data.getSample (src, i0 + 1) - data.getSample (src, i0)) * frac;
                audio.addSample (ch, i, s * g);
            }
            p.pos += step;
        }
    };
    play (cutOff);
    play (preview);
    auditionShown = preview.pos >= 0 && len > 0 ? (float) (preview.pos / len) : -1.0f;
}

void KalibreSampler::reset()
{
    for (auto& v : voices)
    {
        v.env.reset();
        v.note = -1;
        v.ic1 = v.ic2 = v.ic1r = v.ic2r = 0;
        for (auto& g : v.grains) g.on = false;
    }
    seedGrains();
    bend = 0;
    preview = {};
    cutOff = {};
    auditionShown = -1;
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
    loudness = 0;
    ceilingGain = 1;
}

void TromboleseInstrument::reset()
{
    if (voice) voice->voice.reset();
    heldCount = 0;
    note = -1;
    breath = 0;
    bend = wheel = 0;
    loudness = 0;
    ceilingGain = 1;
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
        // The level slides in decibels: the notes' levels differ a hundredfold and more.
        level = std::exp (std::log (jmax (1.0e-9, level)) + (std::log (jmax (1.0e-9, levelTarget)) - std::log (jmax (1.0e-9, level))) * glide);
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
        // The ceiling: the peaks followed (caught on the very sample, let go over 150 ms) and
        // turned down to it at once, back up slowly. Under it, nothing changes.
        constexpr double ceiling = 0.4;
        const double fall = std::exp (-1.0 / (0.150 * rate));
        const double recover = 1.0 - std::exp (-1.0 / (0.100 * rate));
        for (int i = 0; i < count; ++i)
        {
            const double y = block[(size_t) i] * level;
            loudness = jmax (std::abs (y), loudness * fall);
            const double wanted = loudness > ceiling ? ceiling / loudness : 1.0;
            ceilingGain = wanted < ceilingGain ? wanted : ceilingGain + (wanted - ceilingGain) * recover;
            const float x = (float) (y * ceilingGain * gain);
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
