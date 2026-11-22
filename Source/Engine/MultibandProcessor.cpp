#include "MultibandProcessor.h"

#include "Editors.h"
#include "PluginManager.h"
#include "Theme.h"

#include <juce_dsp/juce_dsp.h>

namespace kal
{
using namespace juce;

std::function<void (std::function<void (const PluginRef&)>)> MultibandProcessor::choosePlugin;

// Linkwitz-Riley (24 dB an octave) crossovers, as multiband processors split: at each
// crossover the rest splits into below and above; each band below is turned by an
// all-pass at every crossover above its own, so the bands add back up to the input's
// level at every frequency (only the phase turns, near the crossovers).
struct MultibandProcessor::Dsp
{
    std::array<dsp::LinkwitzRileyFilter<float>, maxBands - 1> splits;
    std::array<std::array<dsp::LinkwitzRileyFilter<float>, maxBands - 1>, maxBands> align;   // [band][crossover above it]
    dsp::FFT fft { fftOrder };
};

PluginRef multibandRef()
{
    PluginRef r;
    r.format = "Kalibre";
    r.identifier = "multiband";
    r.name = "Multiband";
    return r;
}

namespace
{
String hz (float f) { return f >= 1000.0f ? ui::number (f / 1000.0f, f >= 10000.0f ? 0 : 1) + " kHz" : String (roundToInt (f)) + " Hz"; }

NormalisableRange<float> crossoverRange()
{
    NormalisableRange<float> r (MultibandProcessor::lowest, MultibandProcessor::highest);
    r.setSkewForCentre (735.0f);   // the knob's middle is the spectrum's middle, in octaves
    return r;
}

// A band's plug-in in a window of its own: "Multiband  Low: Pro-C 2".
class BandWindow : public DocumentWindow
{
public:
    BandWindow (const String& title, AudioProcessorEditor* editor, std::function<void()> closed)
        : DocumentWindow (title, theme::ground, DocumentWindow::closeButton), onClosed (std::move (closed))
    {
        setUsingNativeTitleBar (true);
        setContentNonOwned (editor, true);
        content.reset (editor);
        setResizable (editor->isResizable(), false);
        if (auto* k = editor->getConstrainer()) setConstrainer (k);
    }
    ~BandWindow() override { empty(); }
    void empty() { setConstrainer (nullptr); clearContentComponent(); content.reset(); }   // the plug-in told first (0056)
    void closeButtonPressed() override { MessageManager::callAsync ([f = onClosed] { if (f) f(); }); }
    std::function<void()> onClosed;

private:
    editors::Owned content;
};
} // namespace

MultibandProcessor::MultibandProcessor (PluginManager& pm)
    : AudioPluginInstance (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                            .withOutput ("Output", AudioChannelSet::stereo(), true)),
      plugins (pm), dspState (std::make_unique<Dsp>())
{
    auto count = std::make_unique<AudioParameterChoice> (ParameterID { "bands", 1 }, "Bands", StringArray { "2", "3", "4" }, 1);
    bandCount = count.get();
    addHostedParameter (std::move (count));
    const float defaults[] { 150.0f, 1500.0f, 6000.0f };
    for (int k = 0; k < maxBands - 1; ++k)
    {
        auto x = std::make_unique<AudioParameterFloat> (ParameterID { "x" + String (k + 1), 1 }, "Crossover " + String (k + 1), crossoverRange(), defaults[k],
                                                        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return hz (v); }));
        crossover[(size_t) k] = x.get();
        addHostedParameter (std::move (x));
    }
    for (int b = 0; b < maxBands; ++b)
    {
        auto& band = bandsArray[(size_t) b];
        const String n (b + 1), id ("b" + n + "_");
        auto on = std::make_unique<AudioParameterBool> (ParameterID { id + "on", 1 }, "Band " + n + " On", true);
        auto solo = std::make_unique<AudioParameterBool> (ParameterID { id + "solo", 1 }, "Band " + n + " Solo", false);
        auto gain = std::make_unique<AudioParameterFloat> (ParameterID { id + "gain", 1 }, "Band " + n + " Volume", NormalisableRange<float> (-60.0f, 12.0f, 0.1f, 2.5f), 0.0f,
                                                           AudioParameterFloatAttributes().withLabel ("dB"));
        band.on = on.get();
        band.solo = solo.get();
        band.gainDb = gain.get();
        addHostedParameter (std::move (on));
        addHostedParameter (std::move (solo));
        addHostedParameter (std::move (gain));
    }
    shown.fill (-96.0f);   // the picture starts from silence
    for (auto& f : dspState->splits) f.setType (dsp::LinkwitzRileyFilterType::lowpass);
    for (auto& row : dspState->align) for (auto& f : row) f.setType (dsp::LinkwitzRileyFilterType::allpass);
}

MultibandProcessor::~MultibandProcessor()
{
    *alive = false;
    for (auto& w : windows) w.reset();   // the plug-ins' windows go before them
    for (auto& b : bandsArray)
        for (auto& s : b.slots)
            if (s.plugin) editors::close (s.plugin->getActiveEditor());
}

void MultibandProcessor::fillInPluginDescription (PluginDescription& d) const
{
    d.name = "Multiband";
    d.pluginFormatName = "Kalibre";
    d.manufacturerName = "Kalibre";
    d.fileOrIdentifier = "multiband";
    d.category = "Effect";
    d.numInputChannels = 2;
    d.numOutputChannels = 2;
}

String MultibandProcessor::bandName (int b, int count)
{
    if (count == 2) return b == 0 ? "Low" : "High";
    if (count == 3) return b == 0 ? "Low" : b == 1 ? "Mid" : "High";
    const char* four[] { "Low", "Low Mid", "High Mid", "High" };
    return four[jlimit (0, 3, b)];
}

//==============================================================================
float MultibandProcessor::crossoverHz (int k) const
{
    // In order and apart, whatever automation asks: each at least minRatio above the one below.
    float f = lowest;
    for (int i = 0; i <= k; ++i)
    {
        const float lo = i == 0 ? lowest : f * minRatio;
        const float hi = highest / std::pow (minRatio, (float) (bands() - 2 - i));
        f = jlimit (lo, jmax (lo, hi), crossover[(size_t) i]->get());
    }
    return f;
}

void MultibandProcessor::setCrossover (int k, float wanted)
{
    if (k < 0 || k >= bands() - 1) return;
    const float lo = k == 0 ? lowest : crossoverHz (k - 1) * minRatio;
    const float hi = k == bands() - 2 ? highest : crossoverHz (k + 1) / minRatio;
    auto* p = crossover[(size_t) k];
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (jlimit (lo, jmax (lo, hi), wanted)));
    p->endChangeGesture();
    changed();
}

void MultibandProcessor::setBands (int count)
{
    count = jlimit (2, maxBands, count);
    // The bands no longer shown keep their plug-ins (back when shown again), and their windows close.
    for (int b = count; b < maxBands; ++b) for (int s = 0; s < perBand; ++s) closeWindow (b, s);
    bandCount->beginChangeGesture();
    bandCount->setValueNotifyingHost (bandCount->convertTo0to1 ((float) (count - 2)));
    bandCount->endChangeGesture();
    updateLatency();
    changed();
}

void MultibandProcessor::changed() { if (onChanged) onChanged(); }

//==============================================================================
void MultibandProcessor::prepareSlot (Slot& s)
{
    if (! s.plugin) return;
    s.plugin->setPlayConfigDetails (2, 2, rate, block);
    s.plugin->prepareToPlay (rate, block);
}

void MultibandProcessor::updateLatency()
{
    int most = 0;
    for (int b = 0; b < bands(); ++b)
    {
        int l = 0;
        for (auto& s : bandsArray[(size_t) b].slots) if (s.plugin) l += s.plugin->getLatencySamples();
        bandsArray[(size_t) b].latency = l;
        most = jmax (most, l);
    }
    {
        const ScopedLock sl (lock);
        for (auto& band : bandsArray)
        {
            band.held.setSize (2, jmax (1, most + block + 1));
            band.held.clear();
            band.heldAt = 0;
        }
    }
    if (most != getLatencySamples()) setLatencySamples (most);
}

bool MultibandProcessor::loadPlugin (int b, int i, const PluginRef& ref, String& error)
{
    if (b < 0 || b >= maxBands || i < 0 || i >= perBand) return false;
    auto instance = plugins.create (ref, rate, block, error);
    if (! instance) return false;
    instance->setPlayConfigDetails (2, 2, rate, block);
    instance->prepareToPlay (rate, block);
    closeWindow (b, i);
    {
        const ScopedLock sl (lock);
        auto& s = bandsArray[(size_t) b].slots[(size_t) i];
        if (s.plugin) editors::close (s.plugin->getActiveEditor());
        s.ref = ref;
        s.plugin = std::move (instance);
    }
    updateLatency();
    changed();
    return true;
}

void MultibandProcessor::clearSlot (int b, int i)
{
    if (b < 0 || b >= maxBands || i < 0 || i >= perBand) return;
    closeWindow (b, i);
    std::unique_ptr<AudioPluginInstance> gone;
    {
        const ScopedLock sl (lock);
        auto& slots = bandsArray[(size_t) b].slots;
        if (slots[(size_t) i].plugin) editors::close (slots[(size_t) i].plugin->getActiveEditor());
        gone = std::move (slots[(size_t) i].plugin);
        slots[(size_t) i] = Slot();
        // The ones after it move up, so the used ones stay together.
        for (int k = i; k + 1 < perBand; ++k) std::swap (slots[(size_t) k], slots[(size_t) k + 1]);
    }
    // Windows follow their plug-ins up a place.
    for (int k = i; k + 1 < perBand; ++k) std::swap (windows[(size_t) (b * perBand + k)], windows[(size_t) (b * perBand + k + 1)]);
    gone.reset();
    updateLatency();
    changed();
}

void MultibandProcessor::showWindow (int b, int i)
{
    if (b < 0 || b >= maxBands || i < 0 || i >= perBand) return;
    auto* plug = bandsArray[(size_t) b].slots[(size_t) i].plugin.get();
    if (plug == nullptr) return;
    auto& w = windows[(size_t) (b * perBand + i)];
    if (! w)
    {
        std::weak_ptr<bool> token = alive;
        auto made = std::make_unique<BandWindow> ("Multiband  " + bandName (b, bands()) + ": " + String (bandsArray[(size_t) b].slots[(size_t) i].ref.name),
                                                  editors::open (*plug), [this, token, b, i] { if (! token.expired()) closeWindow (b, i); });
        if (auto* top = TopLevelWindow::getActiveTopLevelWindow())
            made->setTopLeftPosition (top->getScreenBounds().getCentre() + Point<int> (40 * (b * perBand + i) - made->getWidth() / 2, 30 * b - made->getHeight() / 2));
        else made->centreWithSize (made->getWidth(), made->getHeight());
        w = std::move (made);
    }
    w->setVisible (true);
    w->toFront (true);
    changed();
}

void MultibandProcessor::closeWindow (int b, int i)
{
    if (b < 0 || b >= maxBands || i < 0 || i >= perBand) return;
    auto& w = windows[(size_t) (b * perBand + i)];
    if (! w) return;
    // Its plug-in's window goes now; the window itself a little later (0115).
    if (auto* bw = dynamic_cast<BandWindow*> (w.get())) { bw->onClosed = nullptr; bw->setVisible (false); bw->empty(); }
    editors::retire (std::move (w));
    changed();
}

//==============================================================================
void MultibandProcessor::prepareToPlay (double sr, int bs)
{
    rate = sr;
    block = bs;
    {
        const ScopedLock sl (lock);
        rest.setSize (2, bs);
        dsp::ProcessSpec spec { sr, (uint32) bs, 2 };
        for (auto& f : dspState->splits) { f.prepare (spec); f.reset(); }
        for (auto& row : dspState->align) for (auto& f : row) { f.prepare (spec); f.reset(); }
        for (auto& band : bandsArray)
        {
            band.buffer.setSize (2, bs);
            for (auto& s : band.slots) prepareSlot (s);
        }
    }
    updateLatency();
}

void MultibandProcessor::releaseResources()
{
    for (auto& band : bandsArray) for (auto& s : band.slots) if (s.plugin) s.plugin->releaseResources();
}

void MultibandProcessor::reset()
{
    const ScopedLock sl (lock);
    for (auto& f : dspState->splits) f.reset();
    for (auto& row : dspState->align) for (auto& f : row) f.reset();
    for (auto& band : bandsArray)
    {
        band.held.clear();
        band.heldAt = 0;
        for (auto& s : band.slots) if (s.plugin) s.plugin->reset();
    }
}

double MultibandProcessor::getTailLengthSeconds() const
{
    double tail = 0;
    for (auto& band : bandsArray) for (auto& s : band.slots) if (s.plugin) tail = jmax (tail, s.plugin->getTailLengthSeconds());
    return tail;
}

void MultibandProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    midi.clear();
    const int n = buffer.getNumSamples();
    // The picture's ring: the input, both sides as one.
    {
        int at = ringAt.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            ring[(size_t) at] = buffer.getNumChannels() > 1 ? 0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i)) : buffer.getSample (0, i);
            at = (at + 1) & (fftSize - 1);
        }
        ringAt.store (at, std::memory_order_release);
    }
    const ScopedTryLock tl (lock);
    if (! tl.isLocked() || buffer.getNumChannels() < 2 || n > rest.getNumSamples()) return;   // passes through
    const int count = bands();
    bool anySolo = false, anything = false;
    for (int b = 0; b < count; ++b)
    {
        const auto& band = bandsArray[(size_t) b];
        anySolo = anySolo || band.solo->get();
        anything = anything || band.hasPlugins() || ! band.on->get() || std::abs (band.gainDb->get()) > 0.0001f;
    }
    // Nothing to do: not split, untouched.
    if (! anything && ! anySolo && getLatencySamples() == 0) return;

    // The split: below and above at each crossover in turn; each band below turned by the
    // crossovers above its own, so they meet in phase again.
    for (int ch = 0; ch < 2; ++ch) rest.copyFrom (ch, 0, buffer, ch, 0, n);
    for (int b = 0; b < count; ++b)
    {
        auto& band = bandsArray[(size_t) b];
        if (b == count - 1)
        {
            for (int ch = 0; ch < 2; ++ch) band.buffer.copyFrom (ch, 0, rest, ch, 0, n);
            break;
        }
        auto& f = dspState->splits[(size_t) b];
        f.setCutoffFrequency (jmin (crossoverHz (b), (float) rate * 0.45f));
        for (int k = b + 1; k < count - 1; ++k)
            dspState->align[(size_t) b][(size_t) k].setCutoffFrequency (jmin (crossoverHz (k), (float) rate * 0.45f));
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* r = rest.getWritePointer (ch);
            auto* out = band.buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
            {
                float low = 0, high = 0;
                f.processSample (ch, r[i], low, high);
                for (int k = b + 1; k < count - 1; ++k) low = dspState->align[(size_t) b][(size_t) k].processSample (ch, low);
                out[i] = low;
                r[i] = high;
            }
        }
    }
    // Each band through its plug-ins (On), held back to the slowest band, at its volume; summed at 64 bits (0081).
    const int most = getLatencySamples();
    for (int ch = 0; ch < 2; ++ch) FloatVectorOperations::clear (buffer.getWritePointer (ch), n);
    for (int b = 0; b < count; ++b)
    {
        auto& band = bandsArray[(size_t) b];
        const bool on = band.on->get();
        if (on)
            for (auto& s : band.slots)
                if (s.plugin)
                {
                    AudioBuffer<float> view (band.buffer.getArrayOfWritePointers(), 2, n);
                    MidiBuffer none;
                    s.plugin->processBlock (view, none);
                }
        const float g = (anySolo && ! band.solo->get()) ? 0.0f : Decibels::decibelsToGain (band.gainDb->get(), -60.0f);
        const int hold = most - (on ? band.latency : 0);
        const int size = band.held.getNumSamples();
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* in = band.buffer.getReadPointer (ch);
            auto* ring2 = band.held.getWritePointer (ch);
            auto* out = buffer.getWritePointer (ch);
            int at = band.heldAt;
            for (int i = 0; i < n; ++i)
            {
                ring2[at] = in[i];
                const int from = (at - hold + size) % size;
                out[i] = (float) ((double) out[i] + (double) g * (double) ring2[from]);
                at = (at + 1) % size;
            }
        }
        band.heldAt = (band.heldAt + n) % size;
    }
}

//==============================================================================
void MultibandProcessor::spectrum (std::array<float, points>& out)
{
    std::array<float, fftSize * 2> data {};
    const int at = ringAt.load (std::memory_order_acquire);
    for (int i = 0; i < fftSize; ++i)
    {
        const float w = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * (float) i / (float) (fftSize - 1));
        data[(size_t) i] = ring[(size_t) ((at + i) & (fftSize - 1))] * w;
    }
    dspState->fft.performFrequencyOnlyForwardTransform (data.data());
    for (int p = 0; p < points; ++p)
    {
        const double f = 20.0 * std::pow (1000.0, (double) p / (points - 1));   // 20 Hz to 20 kHz
        const double bin = f / rate * fftSize;
        const int b0 = jlimit (1, fftSize / 2 - 1, (int) bin), b1 = jlimit (b0, fftSize / 2 - 1, (int) (bin * 1.06) + 1);
        float m = 0;
        for (int k = b0; k <= b1; ++k) m = jmax (m, data[(size_t) k]);
        const float db = Decibels::gainToDecibels (m / (fftSize * 0.25f), -96.0f);
        // Falls slowly, rises at once, so it reads calmly.
        shown[(size_t) p] = db > shown[(size_t) p] ? db : shown[(size_t) p] - 1.5f;
        out[(size_t) p] = shown[(size_t) p];
    }
}

//==============================================================================
void MultibandProcessor::getStateInformation (MemoryBlock& out)
{
    XmlElement xml ("Multiband");
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<AudioProcessorParameterWithID*> (p))
            xml.setAttribute (withId->paramID, (double) p->getValue());
    for (int b = 0; b < maxBands; ++b)
        for (int i = 0; i < perBand; ++i)
        {
            const auto& s = bandsArray[(size_t) b].slots[(size_t) i];
            if (! s.used()) continue;
            auto* e = xml.createNewChildElement ("Slot");
            e->setAttribute ("band", b);
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

void MultibandProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (! xml || ! xml->hasTagName ("Multiband")) return;
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<AudioProcessorParameterWithID*> (p))
            if (xml->hasAttribute (withId->paramID))
                p->setValueNotifyingHost ((float) xml->getDoubleAttribute (withId->paramID));
    for (auto* e : xml->getChildWithTagNameIterator ("Slot"))
    {
        const int b = jlimit (0, maxBands - 1, e->getIntAttribute ("band"));
        const int i = jlimit (0, perBand - 1, e->getIntAttribute ("index"));
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
            if (state.getSize() > 0) bandsArray[(size_t) b].slots[(size_t) i].plugin->setStateInformation (state.getData(), (int) state.getSize());
        }
        else
        {
            // A plug-in that's missing keeps its place: the band passes it by.
            const ScopedLock sl (lock);
            bandsArray[(size_t) b].slots[(size_t) i].ref = r;
        }
    }
    updateLatency();
    changed();
}

//==============================================================================
namespace
{
// The spectrum, its bands, and the crossovers to drag (never past one another).
class SpectrumView : public Component, private Timer
{
public:
    explicit SpectrumView (MultibandProcessor& p) : proc (p) { levels.fill (-96.0f); startTimerHz (30); }

    float xOf (float f) const { return (float) getWidth() * std::log (f / 20.0f) / std::log (1000.0f); }
    float hzAt (float x) const { return 20.0f * std::pow (1000.0f, jlimit (0.0f, 1.0f, x / (float) jmax (1, getWidth()))); }

    void paint (Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (theme::roll);
        g.fillRoundedRectangle (r, theme::cornerRadius);
        const int count = proc.bands();
        for (int b = 0; b < count; ++b)
        {
            const float x0 = xOf (proc.bandLow (b)), x1 = xOf (proc.bandHigh (b));
            g.setColour (b % 2 == 0 ? theme::lane : theme::sunken);
            g.fillRect (Rectangle<float> (x0, r.getY() + 1, x1 - x0, r.getHeight() - 2));
            g.setColour (theme::textSecondary);
            g.setFont (theme::font (theme::smallPoints));
            g.drawText (MultibandProcessor::bandName (b, count), Rectangle<float> (x0 + 6, r.getY() + 4, x1 - x0 - 12, 14), Justification::centredLeft, true);
        }
        // The input's spectrum.
        Path curve;
        for (int i = 0; i < MultibandProcessor::points; ++i)
        {
            const float x = (float) i / (MultibandProcessor::points - 1) * r.getWidth();
            const float y = jmap (jlimit (-84.0f, 0.0f, levels[(size_t) i]), -84.0f, 0.0f, r.getBottom() - 2, r.getY() + 20);
            if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        g.setColour (theme::edge);
        g.strokePath (curve, PathStrokeType (1.2f));
        // The crossovers: a line, a handle, the frequency.
        for (int k = 0; k < count - 1; ++k)
        {
            const float x = xOf (proc.crossoverHz (k));
            const bool hot = k == dragging || k == hovering;
            g.setColour (hot ? theme::accent : theme::control);
            g.fillRect (x - 1.0f, r.getY(), 2.0f, r.getHeight());
            g.fillRoundedRectangle (Rectangle<float> (12.0f, 22.0f).withCentre ({ x, r.getCentreY() }), 4.0f);
            g.setColour (theme::text);
            g.setFont (theme::font (theme::smallPoints));
            g.drawText (hz (proc.crossoverHz (k)), Rectangle<float> (x + 6, r.getBottom() - 18, 70, 14), Justification::centredLeft, false);
        }
    }

    int crossoverNear (float x) const
    {
        int best = -1;
        float dist = 10.0f;
        for (int k = 0; k < proc.bands() - 1; ++k)
            if (const float d = std::abs (xOf (proc.crossoverHz (k)) - x); d < dist) { dist = d; best = k; }
        return best;
    }

    void mouseMove (const MouseEvent& e) override
    {
        const int near = crossoverNear (e.position.x);
        if (near != hovering) { hovering = near; repaint(); }
        setMouseCursor (near >= 0 ? MouseCursor::LeftRightResizeCursor : MouseCursor::NormalCursor);
    }
    void mouseExit (const MouseEvent&) override { hovering = -1; repaint(); }
    void mouseDown (const MouseEvent& e) override { dragging = crossoverNear (e.position.x); }
    void mouseDrag (const MouseEvent& e) override
    {
        if (dragging < 0) return;
        proc.setCrossover (dragging, hzAt (e.position.x));   // kept between its neighbours
        repaint();
    }
    void mouseUp (const MouseEvent&) override { dragging = -1; repaint(); }

    MultibandProcessor& proc;
    std::array<float, MultibandProcessor::points> levels {};
    int dragging = -1, hovering = -1;

private:
    void timerCallback() override
    {
        proc.spectrum (levels);
        repaint();
    }
};

// One band's column: its name and range, On, Solo, its volume, and its plug-ins.
class BandColumn : public Component
{
public:
    BandColumn (MultibandProcessor& p, int index) : proc (p), b (index)
    {
        const auto& band = proc.band (b);
        for (auto* x : { &on, &solo }) { x->setClickingTogglesState (true); addAndMakeVisible (x); }
        attachments.add (new ButtonParameterAttachment (*band.on, on));
        attachments.add (new ButtonParameterAttachment (*band.solo, solo));
        gain.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        gain.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        gain.setTitle (MultibandProcessor::bandName (b, proc.bands()) + " Volume");
        gain.setDoubleClickReturnValue (true, 0.0);
        gainAttachment = std::make_unique<SliderParameterAttachment> (*band.gainDb, gain);
        gain.onValueChange = [this] { gain.setTooltip ("Volume: " + String (proc.band (b).gainDb->get(), 1) + " dB. Double-click for 0 dB."); };
        gain.onValueChange();
        addAndMakeVisible (gain);
        const int used = band.used();
        for (int i = 0; i < jmin (MultibandProcessor::perBand, used + 1); ++i)
        {
            const bool filled = band.slots[(size_t) i].used();
            auto* button = slots.add (new theme::HouseButton (filled ? String (band.slots[(size_t) i].ref.name) : ui::u8 ("Add Plug-in\xe2\x80\xa6"),
                                                              filled ? "Click to open its window; right-click to replace or remove it." : "Put an audio effect on this band."));
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
        if (! MultibandProcessor::choosePlugin) return;
        auto& p = proc;
        const int band = b;
        MultibandProcessor::choosePlugin ([&p, band, i] (const PluginRef& r)
        {
            String error;
            if (p.loadPlugin (band, i, r, error)) p.showWindow (band, i);   // added: its window opens
        });
    }

    void slotMenu (int i)
    {
        PopupMenu m;
        m.addItem ("Show Its Window", proc.band (b).slots[(size_t) i].plugin != nullptr, false, [this, i] { proc.showWindow (b, i); });
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
        g.drawText (MultibandProcessor::bandName (b, proc.bands()), r.removeFromTop (18), Justification::centredLeft, false);
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        g.drawText (hz (proc.bandLow (b)) + ui::u8 (" \xe2\x80\x93 ") + hz (proc.bandHigh (b)), r.removeFromTop (14), Justification::centredLeft, false);
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

    MultibandProcessor& proc;
    int b;
    theme::HouseButton on { "On", "The band's plug-ins on or passed by.", true };
    theme::HouseButton solo { "S", "Solo: with any band soloed, only those are heard.", true };
    Slider gain;
    std::unique_ptr<SliderParameterAttachment> gainAttachment;
    OwnedArray<ButtonParameterAttachment> attachments;
    OwnedArray<theme::HouseButton> slots;
};

class MultibandEditor : public AudioProcessorEditor
{
public:
    explicit MultibandEditor (MultibandProcessor& p) : AudioProcessorEditor (p), proc (p), view (p)
    {
        for (int n = 2; n <= MultibandProcessor::maxBands; ++n)
        {
            auto* b = counts.add (new theme::HouseButton (String (n) + " Bands", "Split the sound into " + String (n) + " bands."));
            b->onClick = [this, n] { proc.setBands (n); };
            addAndMakeVisible (b);
        }
        addAndMakeVisible (view);
        proc.onChanged = [sp = SafePointer<MultibandEditor> (this)] { if (sp) sp->rebuild(); };
        setResizable (true, false);
        setSize (900, 330);
        rebuild();
    }
    ~MultibandEditor() override { proc.onChanged = nullptr; }

    void rebuild()
    {
        columns.clear();
        for (int b = 0; b < proc.bands(); ++b) addAndMakeVisible (columns.add (new BandColumn (proc, b)));
        for (int i = 0; i < counts.size(); ++i) counts[i]->setToggleState (i + 2 == proc.bands(), dontSendNotification);
        resized();
        repaint();
    }

    void paint (Graphics& g) override { g.fillAll (theme::sunken); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto top = r.removeFromTop (theme::px (24));
        for (auto* b : counts) { b->setBounds (top.removeFromLeft (b->idealWidth (theme::smallControl))); top.removeFromLeft (6); }
        r.removeFromTop (6);
        view.setBounds (r.removeFromTop (jmax (70, r.getHeight() / 4)));
        r.removeFromTop (6);
        const int n = columns.size();
        const int w = n > 0 ? (r.getWidth() - 6 * (n - 1)) / n : 0;
        for (auto* c : columns) { c->setBounds (r.removeFromLeft (w)); r.removeFromLeft (6); }
    }

    MultibandProcessor& proc;
    SpectrumView view;
    OwnedArray<theme::HouseButton> counts;
    OwnedArray<BandColumn> columns;
};
} // namespace

AudioProcessorEditor* MultibandProcessor::createEditor() { return new MultibandEditor (*this); }

} // namespace kal
