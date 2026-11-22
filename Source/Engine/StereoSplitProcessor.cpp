#include "StereoSplitProcessor.h"

#include "Theme.h"

#include <juce_dsp/juce_dsp.h>

namespace kal
{
using namespace juce;

PluginRef stereoSplitRef()
{
    PluginRef r;
    r.format = "Kalibre";
    r.identifier = "stereo-split";
    r.name = "Stereo Split";
    return r;
}

// The short-time Fourier transform of Left, Middle, Right: the input in a ring of
// one window; every hop (a quarter window) the window's spectrum, each frequency
// shared out between the three parts by its place, and each part's back into its
// own ring, overlapped and added. Hann windows in and out, a quarter apart, add
// up to 1.5 everywhere: a sample comes out exactly one window later.
struct StereoSplitProcessor::Stft
{
    dsp::FFT fft { fftOrder };
    std::array<float, fftSize> window {};
    AudioBuffer<float> in { 2, fftSize };
    std::array<AudioBuffer<float>, 3> out;   // Left, Middle, Right
    std::array<std::vector<float>, 2> spectrum;
    std::vector<float> work;
    std::array<std::array<float, fftSize / 2 + 1>, 3> mask {};
    int at = 0, count = 0;

    Stft()
    {
        for (int i = 0; i < fftSize; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * (float) i / (float) fftSize);
        for (auto& o : out) o.setSize (2, fftSize);
        for (auto& s : spectrum) s.assign (fftSize * 2, 0.0f);
        work.assign (fftSize * 2, 0.0f);
        clear();
    }
    void clear()
    {
        in.clear();
        for (auto& o : out) o.clear();
        at = count = 0;
    }
};

namespace
{
String hzText (float f) { return f >= 1000.0f ? ui::number (f / 1000.0f, f >= 10000.0f ? 0 : 1) + " kHz" : String (roundToInt (f)) + " Hz"; }
} // namespace

StereoSplitProcessor::StereoSplitProcessor (PluginManager& pm)
    : SplitProcessor (pm, "Stereo Split", "stereo-split"), stft (std::make_unique<Stft>())
{
    auto m = std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Split", StringArray { "Left, Middle, Right", "Middle and Sides" }, 0);
    modeParam = m.get();
    addHostedParameter (std::move (m));
    const char* ids[] { "leftEdge", "rightEdge" };
    const char* names[] { "Left Edge", "Right Edge" };
    for (int k = 0; k < 2; ++k)
    {
        auto e = std::make_unique<AudioParameterFloat> (ParameterID { ids[k], 1 }, names[k], NormalisableRange<float> (-1.0f, 1.0f), k == 0 ? -0.3f : 0.3f,
                                                        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return panText (v); }));
        edgeParam[(size_t) k] = e.get();
        addHostedParameter (std::move (e));
    }
    addPartParameters (left, "left_", "Left");
    addPartParameters (middle, "middle_", "Middle");
    addPartParameters (right, "right_", "Right");
    addPartParameters (sides, "sides_", "Sides");
    shownLevel.fill (-96.0f);
    modeParam->addListener (this);
}

StereoSplitProcessor::~StereoSplitProcessor()
{
    modeParam->removeListener (this);
    cancelPendingUpdate();
}

String StereoSplitProcessor::panText (float p)
{
    if (std::abs (p) < 0.005f) return "Centre";
    return String (roundToInt (std::abs (p) * 100.0f)) + (p < 0 ? "% L" : "% R");
}

float StereoSplitProcessor::pointHz (int i) { return 30.0f * std::pow (16000.0f / 30.0f, (float) i / (float) (points - 1)); }

//==============================================================================
SplitProcessor::InUse StereoSplitProcessor::partsInUse() const
{
    InUse u;
    if (mode() == Mode::middleSides) { u.at[0] = middle; u.at[1] = sides; u.count = 2; }
    else { u.at[0] = left; u.at[1] = middle; u.at[2] = right; u.count = 3; }
    return u;
}

String StereoSplitProcessor::partName (int p) const
{
    const char* names[] { "Left", "Middle", "Right", "Sides" };
    return names[jlimit (0, 3, p)];
}

String StereoSplitProcessor::partDetail (int p) const
{
    if (mode() == Mode::middleSides) return p == sides ? "What differs between the sides" : "What both sides share";
    if (p == left) return "Left of " + panText (edge (0));
    if (p == right) return "Right of " + panText (edge (1));
    return panText (edge (0)) + ui::u8 (" \xe2\x80\x93 ") + panText (edge (1));
}

float StereoSplitProcessor::edge (int k) const
{
    // In order and apart, whatever automation asks.
    const float e0 = jlimit (-1.0f, 1.0f - softness, edgeParam[0]->get());
    if (k == 0) return e0;
    return jlimit (e0 + softness, 1.0f, edgeParam[1]->get());
}

void StereoSplitProcessor::setEdge (int k, float wanted)
{
    if (k < 0 || k > 1) return;
    const float lo = k == 0 ? -1.0f : edge (0) + softness;
    const float hi = k == 0 ? edge (1) - softness : 1.0f;
    auto* p = edgeParam[(size_t) k];
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (jlimit (lo, jmax (lo, hi), wanted)));
    p->endChangeGesture();
    changed();
}

void StereoSplitProcessor::setMode (Mode m)
{
    modeParam->beginChangeGesture();
    modeParam->setValueNotifyingHost (modeParam->convertTo0to1 ((float) (int) m));
    modeParam->endChangeGesture();
    cancelPendingUpdate();
    modeChanged();
}

void StereoSplitProcessor::parameterValueChanged (int, float) { triggerAsyncUpdate(); }   // perhaps on the audio thread
void StereoSplitProcessor::handleAsyncUpdate() { modeChanged(); }
void StereoSplitProcessor::stateLoaded() { cancelPendingUpdate(); modeChanged(); }

void StereoSplitProcessor::modeChanged()
{
    const int wanted = modeParam->getIndex();
    if (wanted == modeInPlay.load()) return;
    {
        const ScopedLock sl (lock);
        modeInPlay = wanted;
        stft->clear();
    }
    // The parts no longer shown keep their plug-ins (back when shown again), and their windows close.
    const auto inUse = partsInUse();
    for (int p = 0; p < maxParts; ++p)
        if (std::find (inUse.begin(), inUse.end(), p) == inUse.end())
            for (int i = 0; i < perPart; ++i) closeWindow (p, i);
    updateLatency();
    changed();
}

//==============================================================================
void StereoSplitProcessor::prepareSplit (double, int) { stft->clear(); }
void StereoSplitProcessor::resetSplit() { stft->clear(); }

void StereoSplitProcessor::listen (const AudioBuffer<float>& buffer, int n)
{
    int at = ringAt.load (std::memory_order_relaxed);
    const bool stereo = buffer.getNumChannels() > 1;
    for (int i = 0; i < n; ++i)
    {
        ringL[(size_t) at] = buffer.getSample (0, i);
        ringR[(size_t) at] = buffer.getSample (stereo ? 1 : 0, i);
        at = (at + 1) & (fftSize - 1);
    }
    ringAt.store (at, std::memory_order_release);
}

void StereoSplitProcessor::split (const AudioBuffer<float>& input, int n)
{
    if (mode() == Mode::middleSides)
    {
        // What both share, in both; what differs, on the left, and its opposite on the right.
        auto& m = partsArray[middle].buffer;
        auto& s = partsArray[sides].buffer;
        auto* l = input.getReadPointer (0);
        auto* r = input.getReadPointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float mid = 0.5f * (l[i] + r[i]), side = 0.5f * (l[i] - r[i]);
            m.setSample (0, i, mid);
            m.setSample (1, i, mid);
            s.setSample (0, i, side);
            s.setSample (1, i, -side);
        }
        return;
    }
    auto& st = *stft;
    for (int i = 0; i < n; ++i)
    {
        for (int ch = 0; ch < 2; ++ch) st.in.setSample (ch, st.at, input.getSample (ch, i));
        for (int k = 0; k < 3; ++k)
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* o = st.out[(size_t) k].getWritePointer (ch);
                partsArray[(size_t) k].buffer.setSample (ch, i, o[st.at]);
                o[st.at] = 0;
            }
        st.at = (st.at + 1) % fftSize;
        if (++st.count == hop) { st.count = 0; frame(); }
    }
}

void StereoSplitProcessor::frame()
{
    auto& st = *stft;
    for (int ch = 0; ch < 2; ++ch)
    {
        auto& s = st.spectrum[(size_t) ch];
        auto* in = st.in.getReadPointer (ch);
        for (int i = 0; i < fftSize; ++i) s[(size_t) i] = in[(st.at + i) % fftSize] * st.window[(size_t) i];
        std::fill (s.begin() + fftSize, s.end(), 0.0f);
        st.fft.performRealOnlyForwardTransform (s.data());
    }
    // Each frequency's place, by how loud it is on each side (-1 left, 1 right), and
    // its share in each part: edges softened over `softness`.
    const float e0 = edge (0), e1 = edge (1), w = softness;
    const auto& L = st.spectrum[0];
    const auto& R = st.spectrum[1];
    for (int j = 0; j <= fftSize / 2; ++j)
    {
        const float ml = std::hypot (L[(size_t) (2 * j)], L[(size_t) (2 * j + 1)]);
        const float mr = std::hypot (R[(size_t) (2 * j)], R[(size_t) (2 * j + 1)]);
        const float p = ml + mr > 1.0e-20f ? (mr - ml) / (ml + mr) : 0.0f;
        const float toLeft = jlimit (0.0f, 1.0f, (e0 + 0.5f * w - p) / w);
        const float toRight = jlimit (0.0f, 1.0f, (p - (e1 - 0.5f * w)) / w);
        st.mask[0][(size_t) j] = toLeft;
        st.mask[2][(size_t) j] = toRight;
        st.mask[1][(size_t) j] = 1.0f - toLeft - toRight;
    }
    // Each part back, windowed again, added in.
    const float scale = 1.0f / 1.5f;
    for (int k = 0; k < 3; ++k)
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto& s = st.spectrum[(size_t) ch];
            const auto& m = st.mask[(size_t) k];
            for (int j = 0; j < fftSize; ++j)
            {
                const float g = m[(size_t) (j <= fftSize / 2 ? j : fftSize - j)];
                st.work[(size_t) (2 * j)] = s[(size_t) (2 * j)] * g;
                st.work[(size_t) (2 * j + 1)] = s[(size_t) (2 * j + 1)] * g;
            }
            st.fft.performRealOnlyInverseTransform (st.work.data());
            auto* o = st.out[(size_t) k].getWritePointer (ch);
            for (int i = 0; i < fftSize; ++i) o[(st.at + i) % fftSize] += st.work[(size_t) i] * st.window[(size_t) i] * scale;
        }
}

//==============================================================================
void StereoSplitProcessor::picture (Picture& out)
{
    std::vector<float> l (fftSize * 2, 0.0f), r (fftSize * 2, 0.0f);
    const int at = ringAt.load (std::memory_order_acquire);
    for (int i = 0; i < fftSize; ++i)
    {
        const float w = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * (float) i / (float) fftSize);
        l[(size_t) i] = ringL[(size_t) ((at + i) & (fftSize - 1))] * w;
        r[(size_t) i] = ringR[(size_t) ((at + i) & (fftSize - 1))] * w;
    }
    for (int s = 0; s < scopeSamples; ++s)
    {
        const int k = (at - scopeSamples + s + fftSize) & (fftSize - 1);
        out.samples[(size_t) (2 * s)] = ringL[(size_t) k];
        out.samples[(size_t) (2 * s + 1)] = ringR[(size_t) k];
    }
    dsp::FFT fft (fftOrder);
    fft.performFrequencyOnlyForwardTransform (l.data());
    fft.performFrequencyOnlyForwardTransform (r.data());
    for (int p = 0; p < points; ++p)
    {
        const double bin = pointHz (p) / rate * fftSize;
        const int b0 = jlimit (1, fftSize / 2 - 1, (int) bin), b1 = jlimit (b0, fftSize / 2 - 1, (int) (bin * 1.07) + 1);
        float sumL = 0, sumR = 0, most = 0;
        for (int k = b0; k <= b1; ++k)
        {
            sumL += l[(size_t) k];
            sumR += r[(size_t) k];
            most = jmax (most, l[(size_t) k], r[(size_t) k]);
        }
        const float db = Decibels::gainToDecibels (most / (fftSize * 0.25f), -96.0f);
        // Falls slowly, rises at once, so it reads calmly.
        shownLevel[(size_t) p] = db > shownLevel[(size_t) p] ? db : shownLevel[(size_t) p] - 1.5f;
        out.level[(size_t) p] = shownLevel[(size_t) p];
        out.pan[(size_t) p] = sumL + sumR > 1.0e-9f ? (sumR - sumL) / (sumL + sumR) : 0.0f;
    }
}

//==============================================================================
namespace
{
// Low to high, through the track colours (content, not words).
Colour frequencyColour (float t)
{
    static const Colour stops[] { Colour (0xffE07A86), Colour (0xffE89A6B), Colour (0xffE3C26F), Colour (0xff9DCB7A),
                                  Colour (0xff7FC8B6), Colour (0xff6FA8DC), Colour (0xffB892D9) };
    const float x = jlimit (0.0f, 1.0f, t) * 6.0f;
    const int i = jmin (5, (int) x);
    return stops[i].interpolatedWith (stops[i + 1], x - (float) i);
}

// The vectorscope: the middle straight up, left and right on the diagonals, louder
// further out; each frequency a dot at its place; the parts shaded behind, and
// (Left, Middle, Right) their edges to drag, never past one another.
class Scope : public Component, private Timer
{
public:
    explicit Scope (StereoSplitProcessor& p) : proc (p) { picture.level.fill (-96.0f); startTimerHz (30); }

    static constexpr float floorDb = -60.0f;
    float radius() const { return jmax (20.0f, jmin ((float) getWidth() * 0.5f - 22.0f, (float) getHeight() - 52.0f)); }
    Point<float> centre() const { return { (float) getWidth() * 0.5f, (float) getHeight() - 26.0f }; }
    Point<float> at (float angle, float r) const { return centre() + Point<float> (r * std::sin (angle), -r * std::cos (angle)); }
    static float angleOf (float pan) { return std::atan (jlimit (-1.0f, 1.0f, pan)); }   // a sound's place: as on the scope
    float out (float db) const { return radius() * jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb); }

    void paint (Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (theme::roll);
        g.fillRoundedRectangle (bounds, theme::cornerRadius);
        const float R = radius();
        const auto c = centre();
        const auto disc = Rectangle<float> (2 * R, 2 * R).withCentre (c);
        const bool lmr = proc.mode() == StereoSplitProcessor::Mode::leftMiddleRight;
        const float half = MathConstants<float>::halfPi;
        g.setFont (theme::font (theme::smallPoints));

        // The parts.
        auto wedge = [&] (float from, float to, Colour colour)
        {
            Path p;
            p.addPieSegment (disc, from, to, 0.0f);
            g.setColour (colour);
            g.fillPath (p);
        };
        auto label = [&] (const String& text, float angle, float r)
        {
            g.setColour (theme::textSecondary);
            g.drawText (text, Rectangle<float> (80, 14).withCentre (at (angle, r)), Justification::centred, false);
        };
        if (lmr)
        {
            const float a0 = angleOf (proc.edge (0)), a1 = angleOf (proc.edge (1));
            wedge (-half, a0, theme::lane);
            wedge (a0, a1, theme::sunken);
            wedge (a1, half, theme::lane);
            label ("Left", 0.5f * (-half + a0), R * 0.55f);
            label ("Middle", 0.5f * (a0 + a1), R * 0.55f);
            label ("Right", 0.5f * (a1 + half), R * 0.55f);
        }
        else
        {
            wedge (-half, half, theme::lane);
            label ("Middle", 0.0f, R * 0.55f);
            label ("Sides", -half * 0.86f, R * 0.8f);
            label ("Sides", half * 0.86f, R * 0.8f);
        }
        // Rings every 12 dB, and the middle, left and right.
        g.setColour (theme::rule);
        for (float db = -48.0f; db <= 0.0f; db += 12.0f)
        {
            Path arc;
            arc.addCentredArc (c.x, c.y, out (db), out (db), 0.0f, -half, half, true);
            g.strokePath (arc, PathStrokeType (1.0f));
        }
        g.drawLine (c.x - R, c.y, c.x + R, c.y, 1.0f);
        for (float a : { -half * 0.5f, 0.0f, half * 0.5f }) g.drawLine (Line<float> (c, at (a, R)), 1.0f);
        g.setColour (theme::textSecondary);
        g.drawText ("L", Rectangle<float> (14, 14).withCentre (at (-half * 0.5f, R + 9)), Justification::centred, false);
        g.drawText ("M", Rectangle<float> (14, 14).withCentre (at (0.0f, R + 9)), Justification::centred, false);
        g.drawText ("R", Rectangle<float> (14, 14).withCentre (at (half * 0.5f, R + 9)), Justification::centred, false);

        // The samples: a sound panned to one side on its diagonal, the opposite of each other out at the sides.
        g.setColour (theme::scrollGrab);
        for (int s = 0; s < StereoSplitProcessor::scopeSamples; ++s)
        {
            const float l = picture.samples[(size_t) (2 * s)], r = picture.samples[(size_t) (2 * s + 1)];
            const float size = std::sqrt (l * l + r * r);
            if (size < 0.001f) continue;
            float x = (r - l), y = (l + r);
            if (y < 0) { x = -x; y = -y; }   // the lower half folded up
            const auto p = at (std::atan2 (x, y), out (Decibels::gainToDecibels (size)));
            g.fillRect (p.x - 0.75f, p.y - 0.75f, 1.5f, 1.5f);
        }
        // Each frequency at its place.
        for (int i = 0; i < StereoSplitProcessor::points; ++i)
        {
            const float db = picture.level[(size_t) i];
            if (db <= floorDb) continue;
            const auto p = at (angleOf (picture.pan[(size_t) i]), out (db));
            g.setColour (frequencyColour ((float) i / (StereoSplitProcessor::points - 1)));
            g.fillEllipse (Rectangle<float> (5.0f, 5.0f).withCentre (p));
        }
        // The edges, to drag.
        if (lmr)
            for (int k = 0; k < 2; ++k)
            {
                const float a = angleOf (proc.edge (k));
                const bool hot = k == dragging || k == hovering;
                g.setColour (hot ? theme::accent : theme::control);
                g.drawLine (Line<float> (c, at (a, R)), 2.0f);
                g.fillEllipse (Rectangle<float> (12.0f, 12.0f).withCentre (at (a, R * 0.8f)));
                g.setColour (theme::text);
                const auto tp = at (a, R * 0.8f) + Point<float> (k == 0 ? -46.0f : 46.0f, -12.0f);
                g.drawText (StereoSplitProcessor::panText (proc.edge (k)), Rectangle<float> (70, 14).withCentre (tp), Justification::centred, false);
            }
        // What the dots are.
        auto key = bounds.reduced (10, 4).removeFromBottom (14);
        g.setColour (theme::textSecondary);
        g.drawText ("Low", key.removeFromLeft (28), Justification::centredLeft, false);
        for (int i = 0; i < 7; ++i)
        {
            g.setColour (frequencyColour ((float) i / 6.0f));
            g.fillEllipse (key.removeFromLeft (10).withSizeKeepingCentre (6, 6));
        }
        g.setColour (theme::textSecondary);
        key.removeFromLeft (4);
        g.drawText ("High", key.removeFromLeft (32), Justification::centredLeft, false);
        g.drawText ("Louder further out", key, Justification::centredRight, false);
    }

    int edgeNear (Point<float> m) const
    {
        if (proc.mode() != StereoSplitProcessor::Mode::leftMiddleRight) return -1;
        const auto c = centre();
        const float d = m.getDistanceFrom (c);
        if (d < radius() * 0.12f || d > radius() * 1.08f) return -1;
        const float a = std::atan2 (m.x - c.x, c.y - m.y);
        int best = -1;
        float near = 0.09f;
        for (int k = 0; k < 2; ++k)
            if (const float diff = std::abs (a - angleOf (proc.edge (k))); diff < near) { near = diff; best = k; }
        return best;
    }

    void mouseMove (const MouseEvent& e) override
    {
        const int near = edgeNear (e.position);
        if (near != hovering) { hovering = near; repaint(); }
        setMouseCursor (near >= 0 ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
    }
    void mouseExit (const MouseEvent&) override { hovering = -1; repaint(); }
    void mouseDown (const MouseEvent& e) override { dragging = edgeNear (e.position); }
    void mouseDrag (const MouseEvent& e) override
    {
        if (dragging < 0) return;
        const auto c = centre();
        const float a = jlimit (-MathConstants<float>::halfPi * 0.5f, MathConstants<float>::halfPi * 0.5f, std::atan2 (e.position.x - c.x, c.y - e.position.y));
        proc.setEdge (dragging, std::tan (a));   // kept apart from the other
        repaint();
    }
    void mouseUp (const MouseEvent&) override { dragging = -1; repaint(); }

    StereoSplitProcessor& proc;
    StereoSplitProcessor::Picture picture;
    int dragging = -1, hovering = -1;

private:
    void timerCallback() override
    {
        proc.picture (picture);
        repaint();
    }
};

class StereoSplitEditor : public AudioProcessorEditor
{
public:
    explicit StereoSplitEditor (StereoSplitProcessor& p) : AudioProcessorEditor (p), proc (p), scope (p)
    {
        lmr.onClick = [this] { proc.setMode (StereoSplitProcessor::Mode::leftMiddleRight); };
        ms.onClick = [this] { proc.setMode (StereoSplitProcessor::Mode::middleSides); };
        addAndMakeVisible (lmr);
        addAndMakeVisible (ms);
        addAndMakeVisible (scope);
        proc.onChanged = [sp = SafePointer<StereoSplitEditor> (this)] { if (sp) sp->rebuild(); };
        setResizable (true, false);
        setSize (900, 330);
        rebuild();
    }
    ~StereoSplitEditor() override { proc.onChanged = nullptr; }

    void rebuild()
    {
        columns.clear();
        for (int part : proc.partsInUse()) addAndMakeVisible (columns.add (makePartColumn (proc, part).release()));
        lmr.setToggleState (proc.mode() == StereoSplitProcessor::Mode::leftMiddleRight, dontSendNotification);
        ms.setToggleState (proc.mode() == StereoSplitProcessor::Mode::middleSides, dontSendNotification);
        resized();
        repaint();
    }

    void paint (Graphics& g) override { g.fillAll (theme::sunken); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto top = r.removeFromTop (theme::px (24));
        for (auto* b : { &lmr, &ms }) { b->setBounds (top.removeFromLeft (b->idealWidth (theme::smallControl))); top.removeFromLeft (6); }
        r.removeFromTop (6);
        scope.setBounds (r.removeFromLeft (jmin (380, r.getWidth() / 2)));
        r.removeFromLeft (6);
        const int n = columns.size();
        const int w = n > 0 ? (r.getWidth() - 6 * (n - 1)) / n : 0;
        for (auto* c : columns) { c->setBounds (r.removeFromLeft (w)); r.removeFromLeft (6); }
    }

    StereoSplitProcessor& proc;
    theme::HouseButton lmr { "Left, Middle, Right", "Split by where each frequency sits: left of the left edge, between the edges, right of the right edge." };
    theme::HouseButton ms { "Middle and Sides", "Split into what both sides share and what differs between them." };
    Scope scope;
    OwnedArray<Component> columns;
};
} // namespace

AudioProcessorEditor* StereoSplitProcessor::createEditor() { return new StereoSplitEditor (*this); }

} // namespace kal
