#include "ContainerProcessor.h"

#include "Editors.h"
#include "PanLaw.h"
#include "PluginManager.h"
#include "Theme.h"

#include <juce_dsp/juce_dsp.h>

namespace kal
{
using namespace juce;

// A three-way Linkwitz-Riley split, as multiband processors do it: below
// the band, the band, above it. The three add up to the input's level at
// every frequency (only the phase turns, near the edges).
struct ContainerProcessor::Slot::Filters
{
    dsp::LinkwitzRileyFilter<float> atLow, atHigh, alignLow;   // splits at each edge; the low part turned as the high split turns the rest
};

std::function<void (std::function<void (const PluginRef&)>)> ContainerProcessor::choosePlugin;

PluginRef containerRef()
{
    PluginRef r;
    r.format = "Kalibre";
    r.identifier = "container";
    r.name = "Container";
    return r;
}

namespace
{
String hz (float f) { return f >= 1000.0f ? String (f / 1000.0f, f >= 10000.0f ? 0 : 1) + " kHz" : String (roundToInt (f)) + " Hz"; }

NormalisableRange<float> hzRange()
{
    NormalisableRange<float> r (20.0f, 20000.0f);
    r.setSkewForCentre (632.0f);   // the middle of the knob is the middle of the spectrum, in octaves
    return r;
}
} // namespace

ContainerProcessor::ContainerProcessor (PluginManager& pm)
    : AudioPluginInstance (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                            .withOutput ("Output", AudioChannelSet::stereo(), true)),
      plugins (pm)
{
    auto match = std::make_unique<AudioParameterBool> (ParameterID { "match", 1 }, "Match Loudness", false);
    matchLoudness = match.get();
    addHostedParameter (std::move (match));
    for (int i = 0; i < slotCount; ++i)
    {
        auto& s = slots[(size_t) i];
        const String n (i + 1), id ("s" + n + "_");
        auto on = std::make_unique<AudioParameterBool> (ParameterID { id + "on", 1 }, n + " On", true);
        auto solo = std::make_unique<AudioParameterBool> (ParameterID { id + "solo", 1 }, n + " Solo", false);
        auto width = std::make_unique<AudioParameterChoice> (ParameterID { id + "width", 1 }, n + " Stereo, Mid or Side",
                                                             StringArray { "Stereo", "Mid", "Side" }, 0);
        auto low = std::make_unique<AudioParameterFloat> (ParameterID { id + "low", 1 }, n + " Band Low", hzRange(), 20.0f,
                                                          AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return hz (v); }));
        auto high = std::make_unique<AudioParameterFloat> (ParameterID { id + "high", 1 }, n + " Band High", hzRange(), 20000.0f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return hz (v); }));
        auto gain = std::make_unique<AudioParameterFloat> (ParameterID { id + "gain", 1 }, n + " Volume", NormalisableRange<float> (-60.0f, 12.0f, 0.1f, 2.5f), 0.0f,
                                                           AudioParameterFloatAttributes().withLabel ("dB"));
        auto pan = std::make_unique<AudioParameterFloat> (ParameterID { id + "pan", 1 }, n + " Pan", NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f);
        s.on = on.get(); s.solo = solo.get(); s.width = width.get(); s.low = low.get(); s.high = high.get(); s.gainDb = gain.get(); s.pan = pan.get();
        addHostedParameter (std::move (on));
        addHostedParameter (std::move (solo));
        addHostedParameter (std::move (width));
        addHostedParameter (std::move (low));
        addHostedParameter (std::move (high));
        addHostedParameter (std::move (gain));
        addHostedParameter (std::move (pan));
    }
}

ContainerProcessor::~ContainerProcessor()
{
    for (auto& s : slots)
        if (s.plugin)
            editors::close (s.plugin->getActiveEditor());
}

void ContainerProcessor::fillInPluginDescription (PluginDescription& d) const
{
    d.name = "Container";
    d.pluginFormatName = "Kalibre";
    d.manufacturerName = "Kalibre";
    d.fileOrIdentifier = "container";
    d.category = "Effect";
    d.numInputChannels = 2;
    d.numOutputChannels = 2;
}

double ContainerProcessor::getTailLengthSeconds() const
{
    double tail = 0;
    for (auto& s : slots) if (s.plugin) tail = jmax (tail, s.plugin->getTailLengthSeconds());
    return tail;
}

int ContainerProcessor::firstEmpty() const
{
    for (int i = 0; i < slotCount; ++i) if (! slots[(size_t) i].used()) return i;
    return slotCount;
}

//==============================================================================
void ContainerProcessor::prepareSlot (Slot& s)
{
    s.latency = s.plugin ? s.plugin->getLatencySamples() : 0;
    s.held.setSize (2, s.latency + 1);
    s.held.clear();
    s.heldAt = 0;
    if (! s.filters) s.filters = std::make_shared<Slot::Filters>();
    dsp::ProcessSpec spec { rate, (uint32) block, 2 };
    for (auto* f : { &s.filters->atLow, &s.filters->atHigh, &s.filters->alignLow }) f->prepare (spec);
    s.filters->alignLow.setType (dsp::LinkwitzRileyFilterType::allpass);
}

void ContainerProcessor::updateLatency()
{
    int total = 0;
    for (auto& s : slots) if (s.plugin) total += s.plugin->getLatencySamples();
    if (total != getLatencySamples()) setLatencySamples (total);
}

bool ContainerProcessor::loadPlugin (int i, const PluginRef& ref, String& error)
{
    if (i < 0 || i >= slotCount) return false;
    auto instance = plugins.create (ref, rate, block, error);
    if (! instance) return false;
    instance->setPlayConfigDetails (2, 2, rate, block);
    instance->prepareToPlay (rate, block);
    {
        const ScopedLock sl (slotLock);
        auto& s = slots[(size_t) i];
        if (s.plugin) editors::close (s.plugin->getActiveEditor());
        s.ref = ref;
        s.plugin = std::move (instance);
        prepareSlot (s);
    }
    updateLatency();
    if (onSlotsChanged) onSlotsChanged();
    return true;
}

void ContainerProcessor::clearSlot (int i)
{
    if (i < 0 || i >= slotCount) return;
    {
        const ScopedLock sl (slotLock);
        auto& s = slots[(size_t) i];
        if (s.plugin) editors::close (s.plugin->getActiveEditor());
        s.plugin.reset();
        s.ref = {};
        prepareSlot (s);
    }
    // The places after it move up, so the used ones stay together.
    for (int k = i; k + 1 < slotCount; ++k) moveSlot (k + 1, k);
    for (auto* p : { (RangedAudioParameter*) slots.back().on, (RangedAudioParameter*) slots.back().solo, (RangedAudioParameter*) slots.back().width,
                     (RangedAudioParameter*) slots.back().low, (RangedAudioParameter*) slots.back().high, (RangedAudioParameter*) slots.back().gainDb,
                     (RangedAudioParameter*) slots.back().pan })
        p->setValueNotifyingHost (p->getDefaultValue());
    updateLatency();
    if (onSlotsChanged) onSlotsChanged();
}

void ContainerProcessor::moveSlot (int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= slotCount || to >= slotCount) return;
    const ScopedLock sl (slotLock);
    auto& a = slots[(size_t) from];
    auto& b = slots[(size_t) to];
    std::swap (a.ref, b.ref);
    std::swap (a.plugin, b.plugin);
    // The settings go with the plug-in; the parameters stay where they are.
    for (auto [pa, pb] : { std::pair<RangedAudioParameter*, RangedAudioParameter*> { a.on, b.on }, { a.solo, b.solo }, { a.width, b.width },
                           { a.low, b.low }, { a.high, b.high }, { a.gainDb, b.gainDb }, { a.pan, b.pan } })
    {
        const float va = pa->getValue(), vb = pb->getValue();
        pa->setValueNotifyingHost (vb);
        pb->setValueNotifyingHost (va);
    }
    prepareSlot (a);
    prepareSlot (b);
    if (onSlotsChanged) onSlotsChanged();
}

void ContainerProcessor::prepareToPlay (double sr, int bs)
{
    rate = sr;
    block = bs;
    toPlugin.setSize (2, bs);
    passing.setSize (2, bs);
    input.setSize (2, bs);
    inMeter.prepare (sr);
    outMeter.prepare (sr);
    const ScopedLock sl (slotLock);
    for (auto& s : slots)
    {
        if (s.plugin)
        {
            s.plugin->setPlayConfigDetails (2, 2, sr, bs);
            s.plugin->prepareToPlay (sr, bs);
        }
        prepareSlot (s);
    }
    updateLatency();
}

void ContainerProcessor::releaseResources()
{
    for (auto& s : slots) if (s.plugin) s.plugin->releaseResources();
}

//==============================================================================
void ContainerProcessor::processSlot (Slot& s, AudioBuffer<float>& x, int n, bool active)
{
    auto* l = x.getWritePointer (0);
    auto* r = x.getWritePointer (1);
    const int size = s.held.getNumSamples();
    auto hold = [&s, size] (int ch, float v)
    {
        s.held.setSample (ch, s.heldAt, v);
        return s.held.getSample (ch, (s.heldAt - s.latency + size) % size);
    };

    if (! active)
    {
        // Off: the sound passes, held back as long as the plug-in would hold it.
        for (int i = 0; i < n; ++i)
        {
            l[i] = hold (0, l[i]);
            r[i] = hold (1, r[i]);
            s.heldAt = (s.heldAt + 1) % size;
        }
        return;
    }

    const bool lowEdge = s.hasLowEdge(), highEdge = s.hasHighEdge();
    auto& atLow = s.filters->atLow;
    auto& atHigh = s.filters->atHigh;
    auto& alignLow = s.filters->alignLow;
    if (lowEdge) atLow.setCutoffFrequency (jmin (s.low->get(), (float) rate * 0.45f));
    if (highEdge) { atHigh.setCutoffFrequency (jmin (s.high->get(), (float) rate * 0.45f)); alignLow.setCutoffFrequency (atHigh.getCutoffFrequency()); }
    const auto width = (Width) s.width->getIndex();

    // What the plug-in hears, and what passes by.
    auto* pl = toPlugin.getWritePointer (0);
    auto* pr = toPlugin.getWritePointer (1);
    auto* dl = passing.getWritePointer (0);
    auto* dr = passing.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        // The band, and the rest: below it (turned to line up) and above it.
        float al = l[i], ar = r[i], rl = 0, rr = 0;
        float in[2] { l[i], r[i] }, band[2], rest[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            float x = in[ch], below = 0, above = 0;
            if (lowEdge) atLow.processSample (ch, x, below, x);
            if (highEdge)
            {
                float upper = 0;
                atHigh.processSample (ch, x, x, upper);
                above = upper;
                if (lowEdge) below = alignLow.processSample (ch, below);
            }
            band[ch] = x;
            rest[ch] = below + above;
        }
        al = band[0]; ar = band[1]; rl = rest[0]; rr = rest[1];
        const float mid = 0.5f * (al + ar), side = 0.5f * (al - ar);
        switch (width)
        {
            case Width::Stereo: pl[i] = al;   pr[i] = ar;   dl[i] = rl;        dr[i] = rr;        break;
            case Width::Mid:    pl[i] = mid;  pr[i] = mid;  dl[i] = rl + side; dr[i] = rr - side; break;
            case Width::Side:   pl[i] = side; pr[i] = side; dl[i] = rl + mid;  dr[i] = rr + mid;  break;
        }
    }

    noMidi.clear();
    AudioBuffer<float> view (toPlugin.getArrayOfWritePointers(), 2, n);
    s.plugin->processBlock (view, noMidi);

    // What the plug-in changed, at its volume and pan, joins what passed by.
    const float g = Decibels::decibelsToGain (s.gainDb->get(), -60.0f);
    const float pan = s.pan->get();
    const auto [panL, panR] = panGains (pan);
    const float gl = g * panL, gr = g * panR;
    for (int i = 0; i < n; ++i)
    {
        float yl = pl[i], yr = pr[i];
        if (width == Width::Mid)  { const float m = 0.5f * (yl + yr); yl = m; yr = m; }
        if (width == Width::Side) { const float sd = 0.5f * (yl + yr); yl = sd; yr = -sd; }
        // Where the plug-in's sound meets what passed it by: added at 64 bits (0081).
        l[i] = (float) ((double) hold (0, dl[i]) + (double) gl * (double) yl);
        r[i] = (float) ((double) hold (1, dr[i]) + (double) gr * (double) yr);
        s.heldAt = (s.heldAt + 1) % size;
    }
}

void ContainerProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    midi.clear();
    const int n = buffer.getNumSamples();
    const ScopedTryLock tl (slotLock);
    if (! tl.isLocked() || n > toPlugin.getNumSamples() || buffer.getNumChannels() < 2) return;   // passes through

    for (int ch = 0; ch < 2; ++ch) input.copyFrom (ch, 0, buffer, ch, 0, n);
    bool anySolo = false;
    for (auto& s : slots) anySolo = anySolo || (s.plugin && s.solo->get());
    for (auto& s : slots)
        if (s.plugin)
            processSlot (s, buffer, n, s.on->get() && (! anySolo || s.solo->get()));

    // Match Loudness: what goes out, turned to the short-term loudness of what came in.
    inMeter.process (input.getReadPointer (0), input.getReadPointer (1), n);
    outMeter.process (buffer.getReadPointer (0), buffer.getReadPointer (1), n);
    const double in = inMeter.shortTerm(), out = outMeter.shortTerm();
    const bool on = matchLoudness->get();
    float target = compensationDb;
    if (! on) target = 0;
    else if (in > -70 && out > -70) target = (float) jlimit (-24.0, 24.0, in - out);
    // Moved there over about a second, so it never pumps.
    const float before = compensationDb;
    compensationDb += (target - compensationDb) * (1.0f - std::exp (-(float) n / (float) rate));
    appliedGainDb = compensationDb;
    if (std::abs (before) > 0.001f || std::abs (compensationDb) > 0.001f)
        buffer.applyGainRamp (0, n, Decibels::decibelsToGain (before), Decibels::decibelsToGain (compensationDb));
}

//==============================================================================
void ContainerProcessor::getStateInformation (MemoryBlock& out)
{
    XmlElement xml ("Container");
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<AudioProcessorParameterWithID*> (p))
            xml.setAttribute (withId->paramID, (double) p->getValue());
    for (int i = 0; i < slotCount; ++i)
    {
        const auto& s = slots[(size_t) i];
        if (! s.used()) continue;
        auto* e = xml.createNewChildElement ("Slot");
        e->setAttribute ("index", i);
        e->setAttribute ("ref", String (s.ref.description));
        e->setAttribute ("format", String (s.ref.format));
        e->setAttribute ("identifier", String (s.ref.identifier));
        e->setAttribute ("name", String (s.ref.name));
        e->setAttribute ("uid", s.ref.uid);
        if (s.plugin)
        {
            MemoryBlock state;
            s.plugin->getStateInformation (state);
            e->setAttribute ("state", state.toBase64Encoding());
        }
    }
    copyXmlToBinary (xml, out);
}

void ContainerProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (! xml || ! xml->hasTagName ("Container")) return;
    for (auto* e : xml->getChildWithTagNameIterator ("Slot"))
    {
        const int i = jlimit (0, slotCount - 1, e->getIntAttribute ("index"));
        PluginRef r;
        r.description = e->getStringAttribute ("ref").toStdString();
        r.format = e->getStringAttribute ("format").toStdString();
        r.identifier = e->getStringAttribute ("identifier").toStdString();
        r.name = e->getStringAttribute ("name").toStdString();
        r.uid = e->getIntAttribute ("uid");
        String error;
        if (loadPlugin (i, r, error))
        {
            MemoryBlock state;
            state.fromBase64Encoding (e->getStringAttribute ("state"));
            if (state.getSize() > 0) slots[(size_t) i].plugin->setStateInformation (state.getData(), (int) state.getSize());
        }
        else
        {
            // A plug-in that's missing keeps its place: the sound passes it by.
            const ScopedLock sl (slotLock);
            slots[(size_t) i].ref = r;
        }
    }
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<AudioProcessorParameterWithID*> (p))
            if (xml->hasAttribute (withId->paramID))
                p->setValueNotifyingHost ((float) xml->getDoubleAttribute (withId->paramID));
    updateLatency();
    if (onSlotsChanged) onSlotsChanged();
}

//==============================================================================
namespace
{
// One plug-in's row: its name (click to see its window), On, Solo, Stereo/Mid/Side,
// the band's two edges, its volume and pan, and a menu.
class SlotRow : public Component
{
public:
    SlotRow (ContainerProcessor& p, int index, std::function<void (int)> show) : proc (p), i (index), showWindow (std::move (show))
    {
        auto& s = proc.slot (i);
        name.onClick = [this] { if (proc.slot (i).used()) showWindow (i); else choose(); };
        addAndMakeVisible (name);
        if (! s.used()) return;
        for (auto* b : { &on, &solo }) { b->setClickingTogglesState (true); addAndMakeVisible (b); }
        attachments.add (new ButtonParameterAttachment (*s.on, on));
        attachments.add (new ButtonParameterAttachment (*s.solo, solo));
        width.onClick = [this]
        {
            PopupMenu m;
            const auto* w = proc.slot (i).width;
            const char* names[] { "Stereo", "Mid", "Side" };
            const char* tips[] { "Stereo: all of it", "Mid: only the middle (what both sides share)", "Side: only the sides (where they differ)" };
            for (int k = 0; k < 3; ++k)
                m.addItem (tips[k], true, w->getIndex() == k, [this, k, names] { auto* p = proc.slot (i).width; p->setValueNotifyingHost (p->convertTo0to1 ((float) k)); update(); (void) names; });
            m.showMenuAsync (PopupMenu::Options().withTargetComponent (&width));
        };
        addAndMakeVisible (width);
        for (auto* k : { &low, &high, &gain, &pan })
        {
            k->setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
            k->setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
            k->onValueChange = [this] { update(); };
            addAndMakeVisible (k);
        }
        low.setTitle ("Band Low");
        high.setTitle ("Band High");
        gain.setTitle ("Volume");
        pan.setTitle ("Pan");
        sliderAttachments.add (new SliderParameterAttachment (*s.low, low));
        sliderAttachments.add (new SliderParameterAttachment (*s.high, high));
        sliderAttachments.add (new SliderParameterAttachment (*s.gainDb, gain));
        sliderAttachments.add (new SliderParameterAttachment (*s.pan, pan));
        low.setDoubleClickReturnValue (true, 20.0);
        high.setDoubleClickReturnValue (true, 20000.0);
        gain.setDoubleClickReturnValue (true, 0.0);
        pan.setDoubleClickReturnValue (true, 0.0);
        menu.onClick = [this] { rowMenu(); };
        addAndMakeVisible (menu);
        update();
    }

    void choose()
    {
        if (! ContainerProcessor::choosePlugin) return;
        ContainerProcessor::choosePlugin ([sp = SafePointer<SlotRow> (this)] (const PluginRef& r)
        {
            if (! sp) return;
            String error;
            auto& proc = sp->proc;
            const int at = sp->i;
            proc.loadPlugin (at, r, error);
        });
    }

    void rowMenu()
    {
        PopupMenu m;
        m.addItem ("Show Its Window", proc.slot (i).plugin != nullptr, false, [this] { showWindow (i); });
        m.addItem (ui::u8 ("Replace\xe2\x80\xa6"), [this] { choose(); });
        m.addSeparator();
        m.addItem ("Move Up", i > 0, false, [this] { proc.moveSlot (i, i - 1); });
        m.addItem ("Move Down", i + 1 < proc.firstEmpty(), false, [this] { proc.moveSlot (i, i + 1); });
        m.addSeparator();
        m.addItem ("Remove", [this] { proc.clearSlot (i); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&menu));
    }

    void update()
    {
        auto& s = proc.slot (i);
        if (! s.used()) return;
        width.setButtonText (s.width->getCurrentChoiceName());
        band = s.hasLowEdge() || s.hasHighEdge() ? hz (s.low->get()) + ui::u8 (" \xe2\x80\x93 ") + hz (s.high->get()) : String ("All");
        low.setTooltip ("The band's low edge: " + hz (s.low->get()) + (s.hasLowEdge() ? String() : String (" (none)")) + ". Double-click for none.");
        high.setTooltip ("The band's high edge: " + hz (s.high->get()) + (s.hasHighEdge() ? String() : String (" (none)")) + ". Double-click for none.");
        gain.setTooltip ("Volume: " + String (s.gainDb->get(), 1) + " dB. Double-click for 0 dB.");
        pan.setTooltip ("Pan. Double-click for the centre.");
        repaint();
    }

    void paint (Graphics& g) override
    {
        g.setColour (theme::rule);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
        if (! proc.slot (i).used()) return;
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        g.drawText (band, bandArea, Justification::centredLeft, true);
        if (proc.slot (i).plugin == nullptr)
        {
            g.setColour (theme::warnText);
            g.drawText ("Missing: passes the sound by", name.getBounds().translated (0, 0), Justification::centredRight, true);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (2, 3);
        const int k = r.getHeight(), gap = theme::px (14);
        menu.setBounds (r.removeFromRight (theme::px (26)));
        r.removeFromRight (gap);
        pan.setBounds (r.removeFromRight (k));
        r.removeFromRight (gap);
        gain.setBounds (r.removeFromRight (k));
        r.removeFromRight (gap);
        bandArea = r.removeFromRight (theme::px (110));
        r.removeFromRight (theme::px (20));
        high.setBounds (r.removeFromRight (k));
        r.removeFromRight (gap);
        low.setBounds (r.removeFromRight (k));
        r.removeFromRight (6);
        width.setBounds (r.removeFromRight (theme::px (56)));
        r.removeFromRight (4);
        solo.setBounds (r.removeFromRight (theme::px (24)));
        r.removeFromRight (2);
        on.setBounds (r.removeFromRight (theme::px (34)));
        r.removeFromRight (6);
        name.setBounds (r);
    }

    ContainerProcessor& proc;
    int i;
    std::function<void (int)> showWindow;
    theme::HouseButton name { proc.slot (i).used() ? String (proc.slot (i).ref.name) : ui::u8 ("Add Plug-in\xe2\x80\xa6"),
                              proc.slot (i).used() ? "Click to see its window here." : "Put an audio effect in the container." };
    theme::HouseButton on { "On", "Switch it on or off: off, the sound passes it by." , true };
    theme::HouseButton solo { "S", "Solo: with any soloed, only those play.", true };
    theme::HouseButton width { "Stereo", "What it hears and changes: all of it, only the middle, or only the sides." };
    theme::HouseButton menu { ui::u8 ("\xe2\x8b\xaf"), "Show its window, replace, move or remove it." };
    Slider low, high, gain, pan;
    Rectangle<int> bandArea;
    String band;
    OwnedArray<ButtonParameterAttachment> attachments;
    OwnedArray<SliderParameterAttachment> sliderAttachments;
};

class ContainerEditor : public AudioProcessorEditor, private Timer
{
public:
    explicit ContainerEditor (ContainerProcessor& p) : AudioProcessorEditor (p), proc (p)
    {
        match.setClickingTogglesState (true);
        matchAttachment = std::make_unique<ButtonParameterAttachment> (*proc.matchLoudness, match);
        addAndMakeVisible (match);
        proc.onSlotsChanged = [sp = SafePointer<ContainerEditor> (this)] { if (sp) sp->rebuild(); };
        setResizable (true, false);
        setSize (1000, 300);
        rebuild();
        startTimerHz (4);
    }

    ~ContainerEditor() override
    {
        proc.onSlotsChanged = nullptr;
        editors::close (inner.getComponent());
    }

    void rebuild()
    {
        rows.clear();
        const int used = proc.firstEmpty();
        for (int i = 0; i < jmin (ContainerProcessor::slotCount, used + 1); ++i)
            addAndMakeVisible (rows.add (new SlotRow (proc, i, [this] (int k) { show (k); })));
        if (shown >= used || ! proc.slot (shown).plugin) shown = -1;
        if (shown < 0) for (int i = 0; i < used; ++i) if (proc.slot (i).plugin) { shown = i; break; }
        show (shown);
    }

    void show (int i)
    {
        editors::close (inner.getComponent());
        inner = nullptr;
        shown = i;
        if (i >= 0)
            if (auto* plug = proc.slot (i).plugin.get())
                if (plug->hasEditor())
                {
                    inner = editors::open (*plug);
                    if (inner) addAndMakeVisible (*inner);
                }
        for (auto* r : rows) r->name.setToggleState (r->i == shown && inner != nullptr, dontSendNotification);
        resized();
        repaint();
    }

    void timerCallback() override
    {
        const float g = proc.appliedGainDb.load();
        const String now = proc.matchLoudness->get() ? "Matching: " + String (g >= 0 ? "+" : "") + String (g, 1) + " dB" : String();
        if (now != reading) { reading = now; repaint(); }
    }

    void paint (Graphics& g) override
    {
        g.fillAll (theme::sunken);
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        g.drawText (reading.isNotEmpty() ? reading : String ("Plug-ins one after another; each on its band, the mid or the sides."),
                    readingArea, Justification::centredLeft, true);
        // Each column's name, centred over the first row's control.
        if (! rows.isEmpty() && rows[0]->proc.slot (0).used())
        {
            g.setFont (theme::font (theme::smallPoints, true));
            const auto* row = rows[0];
            auto put = [&] (const char* text, Rectangle<int> under, Justification j = Justification::centredBottom)
            {
                const auto b = under.translated (row->getX(), 0);
                g.drawText (text, Rectangle<int> (b.getCentreX() - 30, headArea.getY(), 60, headArea.getHeight()), j, false);
            };
            put ("Low", row->low.getBounds());
            put ("High", row->high.getBounds());
            g.drawText ("Band", row->bandArea.translated (row->getX(), 0).withY (headArea.getY()).withHeight (headArea.getHeight()), Justification::bottomLeft, false);
            put ("Volume", row->gain.getBounds());
            put ("Pan", row->pan.getBounds());
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto list = r.removeFromLeft (jmin (r.getWidth(), theme::px (620)));
        auto top = list.removeFromTop (theme::px (24));
        match.setBounds (top.removeFromLeft (match.idealWidth (theme::smallControl)));
        top.removeFromLeft (8);
        readingArea = top;
        list.removeFromTop (4);
        headArea = list.removeFromTop (theme::px (16));
        for (auto* row : rows) row->setBounds (list.removeFromTop (theme::px (30)));
        if (inner)
        {
            r.removeFromLeft (8);
            if (inner->isResizable()) inner->setBounds (r);
            else inner->setTopLeftPosition (r.getTopLeft());
        }
    }

    ContainerProcessor& proc;
    theme::HouseButton match { "Match Loudness", "Keep what comes out as loud as what goes in: short-term loudness (LUFS-S), K-weighted.", true };
    std::unique_ptr<ButtonParameterAttachment> matchAttachment;
    OwnedArray<SlotRow> rows;
    Component::SafePointer<AudioProcessorEditor> inner;
    int shown = -1;
    String reading;
    Rectangle<int> readingArea, headArea;
};
} // namespace

AudioProcessorEditor* ContainerProcessor::createEditor() { return new ContainerEditor (*this); }

} // namespace kal
