#include "SplitProcessor.h"

#include "Editors.h"
#include "PluginManager.h"
#include "Theme.h"

namespace kal
{
using namespace juce;

std::function<void (std::function<void (const PluginRef&)>)> SplitProcessor::choosePlugin;

namespace
{
// A part's plug-in in a window of its own: "Multiband  Low: Pro-C 2".
class PartWindow : public DocumentWindow
{
public:
    PartWindow (const String& title, AudioProcessorEditor* editor, std::function<void()> closed)
        : DocumentWindow (title, theme::ground, DocumentWindow::closeButton), onClosed (std::move (closed))
    {
        setUsingNativeTitleBar (true);
        setContentNonOwned (editor, true);
        content.reset (editor);
        setResizable (editor->isResizable(), false);
        if (auto* k = editor->getConstrainer()) setConstrainer (k);
    }
    ~PartWindow() override { empty(); }
    void empty() { setConstrainer (nullptr); clearContentComponent(); content.reset(); }   // the plug-in told first (0056)
    void closeButtonPressed() override { MessageManager::callAsync ([f = onClosed] { if (f) f(); }); }
    std::function<void()> onClosed;

private:
    editors::Owned content;
};
} // namespace

SplitProcessor::SplitProcessor (PluginManager& pm, const String& name, const String& id)
    : AudioPluginInstance (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                            .withOutput ("Output", AudioChannelSet::stereo(), true)),
      plugins (pm), deviceName (name), identifier (id)
{
}

SplitProcessor::~SplitProcessor()
{
    *alive = false;
    for (auto& w : windows) w.reset();   // the plug-ins' windows go before them
    for (auto& p : partsArray)
        for (auto& s : p.slots)
            if (s.plugin) editors::close (s.plugin->getActiveEditor());
}

void SplitProcessor::addPartParameters (int index, const String& id, const String& name)
{
    auto& p = partsArray[(size_t) index];
    auto on = std::make_unique<AudioParameterBool> (ParameterID { id + "on", 1 }, name + " On", true);
    auto solo = std::make_unique<AudioParameterBool> (ParameterID { id + "solo", 1 }, name + " Solo", false);
    auto gain = std::make_unique<AudioParameterFloat> (ParameterID { id + "gain", 1 }, name + " Volume", NormalisableRange<float> (-60.0f, 12.0f, 0.1f, 2.5f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB"));
    p.on = on.get();
    p.solo = solo.get();
    p.gainDb = gain.get();
    addHostedParameter (std::move (on));
    addHostedParameter (std::move (solo));
    addHostedParameter (std::move (gain));
}

void SplitProcessor::fillInPluginDescription (PluginDescription& d) const
{
    d.name = deviceName;
    d.pluginFormatName = "Kalibre";
    d.manufacturerName = "Kalibre";
    d.fileOrIdentifier = identifier;
    d.category = "Effect";
    d.numInputChannels = 2;
    d.numOutputChannels = 2;
}

void SplitProcessor::changed() { if (onChanged) onChanged(); }

//==============================================================================
void SplitProcessor::prepareSlot (Slot& s)
{
    if (! s.plugin) return;
    s.plugin->setPlayConfigDetails (2, 2, rate, block);
    s.plugin->prepareToPlay (rate, block);
}

void SplitProcessor::updateLatency()
{
    int most = 0;
    for (auto& p : partsArray)
    {
        p.latency = 0;
        for (auto& s : p.slots) if (s.plugin) p.latency += s.plugin->getLatencySamples();
    }
    for (int i : partsInUse()) most = jmax (most, partsArray[(size_t) i].latency);
    const int total = splitLatency() + most;
    {
        const ScopedLock sl (lock);
        for (auto& p : partsArray)
        {
            p.held.setSize (2, jmax (1, most + block + 1));
            p.held.clear();
            p.heldAt = 0;
        }
        dry.setSize (2, total + block + 1);
        dry.clear();
        dryAt = 0;
    }
    if (total != getLatencySamples()) setLatencySamples (total);
}

bool SplitProcessor::loadPlugin (int p, int i, const PluginRef& ref, String& error)
{
    if (p < 0 || p >= maxParts || i < 0 || i >= perPart) return false;
    auto instance = plugins.create (ref, rate, block, error);
    if (! instance) return false;
    instance->setPlayConfigDetails (2, 2, rate, block);
    instance->prepareToPlay (rate, block);
    closeWindow (p, i);
    {
        const ScopedLock sl (lock);
        auto& s = partsArray[(size_t) p].slots[(size_t) i];
        if (s.plugin) editors::close (s.plugin->getActiveEditor());
        s.ref = ref;
        s.plugin = std::move (instance);
    }
    updateLatency();
    changed();
    return true;
}

void SplitProcessor::clearSlot (int p, int i)
{
    if (p < 0 || p >= maxParts || i < 0 || i >= perPart) return;
    closeWindow (p, i);
    std::unique_ptr<AudioPluginInstance> gone;
    {
        const ScopedLock sl (lock);
        auto& slots = partsArray[(size_t) p].slots;
        if (slots[(size_t) i].plugin) editors::close (slots[(size_t) i].plugin->getActiveEditor());
        gone = std::move (slots[(size_t) i].plugin);
        slots[(size_t) i] = Slot();
        // The ones after it move up, so the used ones stay together.
        for (int k = i; k + 1 < perPart; ++k) std::swap (slots[(size_t) k], slots[(size_t) k + 1]);
    }
    // Windows follow their plug-ins up a place.
    for (int k = i; k + 1 < perPart; ++k) std::swap (windows[(size_t) (p * perPart + k)], windows[(size_t) (p * perPart + k + 1)]);
    gone.reset();
    updateLatency();
    changed();
}

void SplitProcessor::showWindow (int p, int i)
{
    if (p < 0 || p >= maxParts || i < 0 || i >= perPart) return;
    auto* plug = partsArray[(size_t) p].slots[(size_t) i].plugin.get();
    if (plug == nullptr) return;
    auto& w = windows[(size_t) (p * perPart + i)];
    if (! w)
    {
        std::weak_ptr<bool> token = alive;
        auto made = std::make_unique<PartWindow> (deviceName + "  " + partName (p) + ": " + String (partsArray[(size_t) p].slots[(size_t) i].ref.name),
                                                  editors::open (*plug), [this, token, p, i] { if (! token.expired()) closeWindow (p, i); });
        if (auto* top = TopLevelWindow::getActiveTopLevelWindow())
            made->setTopLeftPosition (top->getScreenBounds().getCentre() + Point<int> (40 * (p * perPart + i) - made->getWidth() / 2, 30 * p - made->getHeight() / 2));
        else made->centreWithSize (made->getWidth(), made->getHeight());
        w = std::move (made);
    }
    w->setVisible (true);
    w->toFront (true);
    changed();
}

void SplitProcessor::closeWindow (int p, int i)
{
    if (p < 0 || p >= maxParts || i < 0 || i >= perPart) return;
    auto& w = windows[(size_t) (p * perPart + i)];
    if (! w) return;
    // Its plug-in's window goes now; the window itself a little later (0115).
    if (auto* pw = dynamic_cast<PartWindow*> (w.get())) { pw->onClosed = nullptr; pw->setVisible (false); pw->empty(); }
    editors::retire (std::move (w));
    changed();
}

void SplitProcessor::closeWindows()
{
    for (int p = 0; p < maxParts; ++p) for (int i = 0; i < perPart; ++i) closeWindow (p, i);
}

//==============================================================================
void SplitProcessor::prepareToPlay (double sr, int bs)
{
    rate = sr;
    block = bs;
    {
        const ScopedLock sl (lock);
        prepareSplit (sr, bs);
        for (auto& p : partsArray)
        {
            p.buffer.setSize (2, bs);
            for (auto& s : p.slots) prepareSlot (s);
        }
    }
    updateLatency();
}

void SplitProcessor::releaseResources()
{
    for (auto& p : partsArray) for (auto& s : p.slots) if (s.plugin) s.plugin->releaseResources();
}

void SplitProcessor::reset()
{
    const ScopedLock sl (lock);
    resetSplit();
    dry.clear();
    dryAt = 0;
    for (auto& p : partsArray)
    {
        p.held.clear();
        p.heldAt = 0;
        for (auto& s : p.slots) if (s.plugin) s.plugin->reset();
    }
}

double SplitProcessor::getTailLengthSeconds() const
{
    double tail = 0;
    for (auto& p : partsArray) for (auto& s : p.slots) if (s.plugin) tail = jmax (tail, s.plugin->getTailLengthSeconds());
    return tail;
}

void SplitProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    midi.clear();
    const int n = buffer.getNumSamples();
    listen (buffer, n);
    const ScopedTryLock tl (lock);
    if (! tl.isLocked() || buffer.getNumChannels() < 2 || n > partsArray[0].buffer.getNumSamples()) return;   // passes through
    const auto inUse = partsInUse();
    bool anySolo = false, anything = false;
    for (int i : inUse)
    {
        const auto& p = partsArray[(size_t) i];
        anySolo = anySolo || p.solo->get();
        anything = anything || p.hasPlugins() || ! p.on->get() || std::abs (p.gainDb->get()) > 0.0001f;
    }
    const int latency = getLatencySamples();
    const bool needed = anything || anySolo;
    // Nothing to do and no delay of the split's own: not split, untouched.
    if (! needed && latency == 0) return;

    // The input held back by the device's delay: what comes out when nothing's done.
    const int drySize = dry.getNumSamples();
    if (drySize <= latency) return;
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = dry.getWritePointer (ch);
        auto* in = buffer.getReadPointer (ch);
        int at = dryAt;
        for (int i = 0; i < n; ++i) { d[at] = in[i]; at = (at + 1) % drySize; }
    }

    split (buffer, n);

    if (! needed)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = dry.getReadPointer (ch);
            auto* out = buffer.getWritePointer (ch);
            int at = dryAt;
            for (int i = 0; i < n; ++i) { out[i] = d[(at - latency + drySize) % drySize]; at = (at + 1) % drySize; }
        }
        dryAt = (dryAt + n) % drySize;
        return;
    }
    dryAt = (dryAt + n) % drySize;

    // Each part through its plug-ins (On), held back to the slowest part, at its volume; summed at 64 bits (0081).
    const int most = jmax (0, latency - splitLatency());
    for (int ch = 0; ch < 2; ++ch) FloatVectorOperations::clear (buffer.getWritePointer (ch), n);
    for (int index : inUse)
    {
        auto& p = partsArray[(size_t) index];
        const bool on = p.on->get();
        if (on)
            for (auto& s : p.slots)
                if (s.plugin)
                {
                    AudioBuffer<float> view (p.buffer.getArrayOfWritePointers(), 2, n);
                    MidiBuffer none;
                    s.plugin->processBlock (view, none);
                }
        const float g = (anySolo && ! p.solo->get()) ? 0.0f : Decibels::decibelsToGain (p.gainDb->get(), -60.0f);
        const int size = p.held.getNumSamples();
        const int hold = jlimit (0, size - 1, most - (on ? p.latency : 0));
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* in = p.buffer.getReadPointer (ch);
            auto* ring = p.held.getWritePointer (ch);
            auto* out = buffer.getWritePointer (ch);
            int at = p.heldAt;
            for (int i = 0; i < n; ++i)
            {
                ring[at] = in[i];
                const int from = (at - hold + size) % size;
                out[i] = (float) ((double) out[i] + (double) g * (double) ring[from]);
                at = (at + 1) % size;
            }
        }
        p.heldAt = (p.heldAt + n) % size;
    }
}

//==============================================================================
void SplitProcessor::getStateInformation (MemoryBlock& out)
{
    XmlElement xml (deviceName.removeCharacters (" "));
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<AudioProcessorParameterWithID*> (p))
            xml.setAttribute (withId->paramID, (double) p->getValue());
    for (int b = 0; b < maxParts; ++b)
        for (int i = 0; i < perPart; ++i)
        {
            const auto& s = partsArray[(size_t) b].slots[(size_t) i];
            if (! s.used()) continue;
            auto* e = xml.createNewChildElement ("Slot");
            e->setAttribute ("band", b);   // the part (the Multiband's name for it, kept)
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

void SplitProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (! xml || ! xml->hasTagName (deviceName.removeCharacters (" "))) return;
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<AudioProcessorParameterWithID*> (p))
            if (xml->hasAttribute (withId->paramID))
                p->setValueNotifyingHost ((float) xml->getDoubleAttribute (withId->paramID));
    for (auto* e : xml->getChildWithTagNameIterator ("Slot"))
    {
        const int b = jlimit (0, maxParts - 1, e->getIntAttribute ("band"));
        const int i = jlimit (0, perPart - 1, e->getIntAttribute ("index"));
        PluginRef r;
        r.description = e->getStringAttribute ("ref").toStdString();
        r.format = e->getStringAttribute ("format").toStdString();
        r.identifier = e->getStringAttribute ("identifier").toStdString();
        r.name = e->getStringAttribute ("name").toStdString();
        r.uid = e->getIntAttribute ("uid");
        String error;
        if (loadPlugin (b, i, r, error))
        {
            MemoryBlock state;
            state.fromBase64Encoding (e->getStringAttribute ("state"));
            if (state.getSize() > 0) partsArray[(size_t) b].slots[(size_t) i].plugin->setStateInformation (state.getData(), (int) state.getSize());
        }
        else
        {
            // A plug-in that's missing keeps its place: the part passes it by.
            const ScopedLock sl (lock);
            partsArray[(size_t) b].slots[(size_t) i].ref = r;
        }
    }
    stateLoaded();
    updateLatency();
    changed();
}

//==============================================================================
namespace
{
// One part's column: its name and range, On, Solo, its volume, and its plug-ins.
class PartColumn : public Component
{
public:
    PartColumn (SplitProcessor& p, int index) : proc (p), b (index)
    {
        const auto& part = proc.part (b);
        for (auto* x : { &on, &solo }) { x->setClickingTogglesState (true); addAndMakeVisible (x); }
        attachments.add (new ButtonParameterAttachment (*part.on, on));
        attachments.add (new ButtonParameterAttachment (*part.solo, solo));
        gain.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        gain.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        gain.setTitle (proc.partName (b) + " Volume");
        gain.setDoubleClickReturnValue (true, 0.0);
        gainAttachment = std::make_unique<SliderParameterAttachment> (*part.gainDb, gain);
        gain.onValueChange = [this] { gain.setTooltip ("Volume: " + String (proc.part (b).gainDb->get(), 1) + " dB. Double-click for 0 dB."); };
        gain.onValueChange();
        addAndMakeVisible (gain);
        const int used = part.used();
        for (int i = 0; i < jmin (SplitProcessor::perPart, used + 1); ++i)
        {
            const bool filled = part.slots[(size_t) i].used();
            auto* button = slots.add (new theme::HouseButton (filled ? String (part.slots[(size_t) i].ref.name) : ui::u8 ("Add Plug-in\xe2\x80\xa6"),
                                                              filled ? "Click to open its window; right-click to replace or remove it."
                                                                     : "Put an audio effect on " + proc.partName (b) + "."));
            button->setToggleState (filled && proc.windowOpen (b, i), dontSendNotification);
            button->onClick = [this, i, filled]
            {
                if (! filled) { choose (i); return; }
                if (ModifierKeys::currentModifiers.isPopupMenu()) { slotMenu (i); return; }
                proc.showWindow (b, i);
            };
            addAndMakeVisible (button);
        }
    }

    void choose (int i)
    {
        if (! SplitProcessor::choosePlugin) return;
        auto& p = proc;
        const int part = b;
        SplitProcessor::choosePlugin ([&p, part, i] (const PluginRef& r)
        {
            String error;
            if (p.loadPlugin (part, i, r, error)) p.showWindow (part, i);   // added: its window opens
        });
    }

    void slotMenu (int i)
    {
        PopupMenu m;
        m.addItem ("Show Its Window", proc.part (b).slots[(size_t) i].plugin != nullptr, false, [this, i] { proc.showWindow (b, i); });
        m.addItem (ui::u8 ("Replace\xe2\x80\xa6"), [this, i] { choose (i); });
        m.addSeparator();
        m.addItem ("Remove", [this, i] { proc.clearSlot (b, i); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (slots[i]));
    }

    void paint (Graphics& g) override
    {
        g.setColour (theme::lane);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), theme::cornerRadius);
        g.setColour (theme::text);
        g.setFont (theme::font (theme::bodyPoints, true));
        auto r = getLocalBounds().reduced (8, 6);
        g.drawText (proc.partName (b), r.removeFromTop (18), Justification::centredLeft, false);
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        g.drawText (proc.partDetail (b), r.removeFromTop (14), Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 6);
        r.removeFromTop (36);
        auto row = r.removeFromTop (theme::px (24));
        on.setBounds (row.removeFromLeft (theme::px (36)));
        row.removeFromLeft (4);
        solo.setBounds (row.removeFromLeft (theme::px (24)));
        row.removeFromLeft (6);
        gain.setBounds (row.removeFromLeft (row.getHeight()));
        r.removeFromTop (6);
        for (auto* s : slots) { s->setBounds (r.removeFromTop (theme::px (24))); r.removeFromTop (4); }
    }

    SplitProcessor& proc;
    int b;
    theme::HouseButton on { "On", "Its plug-ins on, or passed by.", true };
    theme::HouseButton solo { "S", "Solo: with any part soloed, only those are heard.", true };
    Slider gain;
    std::unique_ptr<SliderParameterAttachment> gainAttachment;
    OwnedArray<ButtonParameterAttachment> attachments;
    OwnedArray<theme::HouseButton> slots;
};
} // namespace

std::unique_ptr<Component> makePartColumn (SplitProcessor& p, int part) { return std::make_unique<PartColumn> (p, part); }

} // namespace kal
