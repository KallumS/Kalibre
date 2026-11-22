#include "MultibandProcessor.h"

#include "Editors.h"
#include "PluginManager.h"
#include "Theme.h"

#include <juce_dsp/juce_dsp.h>

namespace kal
{
using namespace juce;

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

} // namespace

MultibandProcessor::MultibandProcessor (PluginManager& pm)
    : SplitProcessor (pm, "Multiband", "multiband"), dspState (std::make_unique<Dsp>())
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
    for (int b = 0; b < maxBands; ++b) addPartParameters (b, "b" + String (b + 1) + "_", "Band " + String (b + 1));
    shown.fill (-96.0f);   // the picture starts from silence
    for (auto& f : dspState->splits) f.setType (dsp::LinkwitzRileyFilterType::lowpass);
    for (auto& row : dspState->align) for (auto& f : row) f.setType (dsp::LinkwitzRileyFilterType::allpass);
}

MultibandProcessor::~MultibandProcessor() = default;

SplitProcessor::InUse MultibandProcessor::partsInUse() const
{
    InUse u;
    for (int b = 0; b < bands(); ++b) u.at[(size_t) u.count++] = b;
    return u;
}

String MultibandProcessor::partDetail (int b) const { return hz (bandLow (b)) + ui::u8 (" \xe2\x80\x93 ") + hz (bandHigh (b)); }

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

//==============================================================================
void MultibandProcessor::prepareSplit (double sr, int bs)
{
    rest.setSize (2, bs);
    dsp::ProcessSpec spec { sr, (uint32) bs, 2 };
    for (auto& f : dspState->splits) { f.prepare (spec); f.reset(); }
    for (auto& row : dspState->align) for (auto& f : row) { f.prepare (spec); f.reset(); }
}

void MultibandProcessor::resetSplit()
{
    for (auto& f : dspState->splits) f.reset();
    for (auto& row : dspState->align) for (auto& f : row) f.reset();
}

void MultibandProcessor::listen (const AudioBuffer<float>& buffer, int n)
{
    // The picture's ring: the input, both sides as one.
    int at = ringAt.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        ring[(size_t) at] = buffer.getNumChannels() > 1 ? 0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i)) : buffer.getSample (0, i);
        at = (at + 1) & (fftSize - 1);
    }
    ringAt.store (at, std::memory_order_release);
}

void MultibandProcessor::split (const AudioBuffer<float>& buffer, int n)
{
    // Below and above at each crossover in turn; each band below turned by the
    // crossovers above its own, so they meet in phase again.
    const int count = bands();
    for (int ch = 0; ch < 2; ++ch) rest.copyFrom (ch, 0, buffer, ch, 0, n);
    for (int b = 0; b < count; ++b)
    {
        auto& band = partsArray[(size_t) b];
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
        for (int b = 0; b < proc.bands(); ++b) addAndMakeVisible (columns.add (makePartColumn (proc, b).release()));
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
    OwnedArray<Component> columns;
};
} // namespace

AudioProcessorEditor* MultibandProcessor::createEditor() { return new MultibandEditor (*this); }

} // namespace kal
