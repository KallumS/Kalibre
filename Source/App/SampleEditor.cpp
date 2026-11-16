#include "SampleEditor.h"

#include "AudioTools.h"

namespace kal
{
using namespace juce;

SampleEditor::SampleEditor (Controller& ctrl) : c (ctrl)
{
    setTitle ("Sample Editor");
    for (auto* b : { &waveButton, &spectrumButton })
    {
        b->setRadioGroupId (7);
        b->setClickingTogglesState (true);
    }
    waveButton.setToggleState (true, dontSendNotification);
    waveButton.onClick = [this] { spectral = false; repaint(); };
    spectrumButton.onClick = [this] { spectral = true; if (spectrogram.isNull()) makeSpectrogram(); repaint(); };
    for (auto* b : { &waveButton, &spectrumButton, &normalise, &gainButton, &silenceButton, &reverseButton, &fadeInButton,
                     &fadeOutButton, &invertButton, &dcButton, &stripButton, &quantiseButton, &zeroButton })
        addAndMakeVisible (b);
    zeroButton.setClickingTogglesState (false);
    zeroButton.onClick = [this] { c.zeroCrossings = ! c.zeroCrossings; c.viewChanged(); };
    zeroButton.setToggleState (c.zeroCrossings, dontSendNotification);

    normalise.onClick = [this] { c.normaliseClip (c.selectedClip); };
    stripButton.onClick = [this] { c.stripSilence (c.selectedClip); };
    quantiseButton.onClick = [this] { c.quantiseAudio (c.selectedClip); };
    silenceButton.onClick = [this] { run ("Silence", [] (AudioBuffer<float>& b, int s, int e) { audiotools::silence (b.getArrayOfWritePointers(), b.getNumChannels(), s, e); }); };
    reverseButton.onClick = [this] { run ("Reverse Range", [] (AudioBuffer<float>& b, int s, int e) { audiotools::reverse (b.getArrayOfWritePointers(), b.getNumChannels(), s, e); }); };
    fadeInButton.onClick = [this] { run ("Fade In", [] (AudioBuffer<float>& b, int s, int e) { audiotools::fade (b.getArrayOfWritePointers(), b.getNumChannels(), s, e, true); }); };
    fadeOutButton.onClick = [this] { run ("Fade Out", [] (AudioBuffer<float>& b, int s, int e) { audiotools::fade (b.getArrayOfWritePointers(), b.getNumChannels(), s, e, false); }); };
    invertButton.onClick = [this] { run ("Invert", [] (AudioBuffer<float>& b, int s, int e) { audiotools::invert (b.getArrayOfWritePointers(), b.getNumChannels(), s, e); }); };
    dcButton.onClick = [this] { run ("Remove DC", [] (AudioBuffer<float>& b, int s, int e) { audiotools::removeDc (b.getArrayOfWritePointers(), b.getNumChannels(), s, e); }); };
    gainButton.onClick = [this]
    {
        PopupMenu m;
        for (double db : { -12.0, -6.0, -3.0, -1.0, 1.0, 3.0, 6.0, 12.0 })
            m.addItem ((db > 0 ? "+" : "") + String (db, 0) + " dB", [this, db]
            {
                const float g = Decibels::decibelsToGain ((float) db);
                run ("Gain", [g] (AudioBuffer<float>& b, int s, int e) { audiotools::gain (b.getArrayOfWritePointers(), b.getNumChannels(), s, e, g); });
            });
        m.addSeparator();
        m.addItem ("Normalise the Range to -0.3 dB", [this]
        {
            run ("Normalise Range", [] (AudioBuffer<float>& b, int s, int e)
            {
                const float pk = audiotools::peak (b.getArrayOfReadPointers(), b.getNumChannels(), s, e);
                if (pk > 0) audiotools::gain (b.getArrayOfWritePointers(), b.getNumChannels(), s, e,
                                              Decibels::decibelsToGain ((float) audiotools::normaliseGainDb (pk)));
            });
        });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (&gainButton));
    };
    c.addChangeListener (this);
}

SampleEditor::~SampleEditor() { c.removeChangeListener (this); }

void SampleEditor::changeListenerCallback (ChangeBroadcaster*)
{
    zeroButton.setToggleState (c.zeroCrossings, dontSendNotification);
    if (isVisible()) load();
}

void SampleEditor::load()
{
    const auto* cl = c.clip();
    if (! cl || cl->isMidi())
    {
        loadedClip = 0;
        audio.setSize (0, 0);
        repaint();
        return;
    }
    const auto f = c.fileOf (*cl, false);
    const auto path = f.getFullPathName().toStdString();
    if (cl->id == loadedClip && path == loadedFile) { repaint(); return; }
    if (cl->id != loadedClip) selStart = selEnd = -1;
    loadedClip = cl->id;
    loadedFile = path;
    if (! Controller::readAudio (f, audio, rate)) audio.setSize (0, 0);
    hits.clear();
    for (int s : audiotools::transients (audio.getArrayOfReadPointers(), audio.getNumChannels(), audio.getNumSamples(), rate, 0.5))
        hits.push_back (s);
    peaksWidth = 0;
    spectrogram = {};
    if (spectral) makeSpectrogram();
    repaint();
}

Rectangle<int> SampleEditor::waveArea() const
{
    return getLocalBounds().withTrimmedTop (theme::px (36)).reduced (theme::px (8), theme::px (4));
}

int SampleEditor::sampleAt (float x) const
{
    const auto a = waveArea();
    const int n = audio.getNumSamples();
    return jlimit (0, n, (int) ((x - (float) a.getX()) / (float) jmax (1, a.getWidth()) * (float) n));
}

float SampleEditor::xOf (int sample) const
{
    const auto a = waveArea();
    return (float) a.getX() + (float) sample / (float) jmax (1, audio.getNumSamples()) * (float) a.getWidth();
}

void SampleEditor::makePeaks()
{
    const int w = waveArea().getWidth();
    peaksWidth = w;
    peaks.assign ((size_t) jmax (0, w) * 2, 0.0f);
    const int n = audio.getNumSamples();
    if (n == 0 || w <= 0) return;
    for (int x = 0; x < w; ++x)
    {
        const int a = (int) ((int64) x * n / w), b = jmax (a + 1, (int) ((int64) (x + 1) * n / w));
        float lo = 0, hi = 0;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        {
            auto r = audio.findMinMax (ch, a, jmin (b, n) - a);
            lo = jmin (lo, r.getStart());
            hi = jmax (hi, r.getEnd());
        }
        peaks[(size_t) x * 2] = lo;
        peaks[(size_t) x * 2 + 1] = hi;
    }
}

void SampleEditor::makeSpectrogram()
{
    const int n = audio.getNumSamples();
    if (n < 2048) { spectrogram = {}; return; }
    const int size = 2048;
    const int columns = jlimit (64, 1600, n / 512);
    const int rows = 256;
    spectrogram = Image (Image::RGB, columns, rows, true);
    std::vector<float> mono ((size_t) size);
    // Rows on a log scale of frequency, 30 Hz to the top.
    const double top = rate / 2, bottom = 30.0;
    for (int x = 0; x < columns; ++x)
    {
        const int start = jlimit (0, n - size, (int) ((int64) x * (n - size) / jmax (1, columns - 1)));
        for (int i = 0; i < size; ++i)
        {
            float v = 0;
            for (int ch = 0; ch < audio.getNumChannels(); ++ch) v += audio.getSample (ch, start + i);
            mono[(size_t) i] = v / (float) audio.getNumChannels();
        }
        const auto db = audiotools::spectrumDb (mono.data(), size);
        for (int y = 0; y < rows; ++y)
        {
            const double hz = bottom * std::pow (top / bottom, (double) (rows - 1 - y) / (rows - 1));
            const int bin = jlimit (0, (int) db.size() - 1, (int) (hz / rate * size));
            const float level = jlimit (0.0f, 1.0f, (db[(size_t) bin] + 96.0f) / 96.0f);
            // From the ground to the controls' grey: brightness is loudness (never the accent).
            spectrogram.setPixelAt (x, y, theme::roll.interpolatedWith (theme::playhead, level * level));
        }
    }
}

std::pair<int, int> SampleEditor::range() const
{
    if (selStart >= 0 && selEnd > selStart) return { selStart, selEnd };
    if (const auto* cl = c.project.clip (loadedClip))
    {
        const int a = (int) (cl->audio.sourceStart * rate);
        const int b = cl->audio.sourceLength > 0 ? (int) ((cl->audio.sourceStart + cl->audio.sourceLength) * rate) : audio.getNumSamples();
        return { jlimit (0, audio.getNumSamples(), a), jlimit (0, audio.getNumSamples(), b) };
    }
    return { 0, audio.getNumSamples() };
}

void SampleEditor::run (const String& label, std::function<void (AudioBuffer<float>&, int, int)> op)
{
    const auto [a, b] = range();
    c.editAudio (c.selectedClip, label, a, b, std::move (op));
}

void SampleEditor::paint (Graphics& g)
{
    g.fillAll (theme::sunken);
    const auto area = waveArea();
    g.setColour (theme::roll);
    g.fillRect (area);
    if (audio.getNumSamples() == 0)
    {
        g.setColour (theme::textSecondary);
        g.setFont (theme::font());
        g.drawFittedText ("The clip's audio file couldn't be read.", area, Justification::centred, 2);
        return;
    }
    // The part the clip plays.
    if (const auto* cl = c.project.clip (loadedClip))
    {
        const int a = (int) (cl->audio.sourceStart * rate);
        const int b = cl->audio.sourceLength > 0 ? (int) ((cl->audio.sourceStart + cl->audio.sourceLength) * rate) : audio.getNumSamples();
        g.setColour (theme::lane);
        g.fillRect (Rectangle<float> (xOf (a), (float) area.getY(), xOf (b) - xOf (a), (float) area.getHeight()));
    }
    if (spectral && spectrogram.isValid())
        g.drawImage (spectrogram, area.toFloat(), RectanglePlacement::stretchToFit);
    else
    {
        if (peaksWidth != area.getWidth()) makePeaks();
        const float mid = (float) area.getCentreY(), half = (float) area.getHeight() * 0.48f;
        g.setColour (theme::control);
        for (int x = 0; x < peaksWidth; ++x)
        {
            const float lo = peaks[(size_t) x * 2], hi = peaks[(size_t) x * 2 + 1];
            g.drawVerticalLine (area.getX() + x, mid - hi * half, mid - lo * half + 1.0f);
        }
        g.setColour (theme::rule);
        g.drawHorizontalLine ((int) mid, (float) area.getX(), (float) area.getRight());
    }
    // The hits: thin marks at the top edge.
    g.setColour (theme::edge);
    for (int h : hits) g.fillRect (Rectangle<float> (xOf (h), (float) area.getY(), 1.0f, (float) theme::px (8)));
    // The chosen range, in the accent's outline.
    if (selStart >= 0 && selEnd > selStart)
    {
        const Rectangle<float> r (xOf (selStart), (float) area.getY(), jmax (1.0f, xOf (selEnd) - xOf (selStart)), (float) area.getHeight());
        g.setColour (theme::accent.withAlpha (0.18f));
        g.fillRect (r);
        g.setColour (theme::accent);
        g.drawRect (r, 1.0f);
    }
    g.setColour (theme::textSecondary);
    g.setFont (theme::font (theme::smallPoints));
    const auto [a, b] = range();
    const String what = selStart >= 0 && selEnd > selStart ? "Chosen: " : "The clip's part: ";
    g.drawText (what + String ((b - a) / rate, 3) + " s  " + ui::u8 ("\xc2\xb7") + "  " + String ((int) hits.size()) + " hits  "
                    + ui::u8 ("\xc2\xb7") + "  " + String (rate / 1000.0, 1) + " kHz, " + String (audio.getNumChannels()) + " ch",
                area.reduced (6, 4), Justification::bottomLeft, false);
}

void SampleEditor::resized()
{
    auto bar = getLocalBounds().removeFromTop (theme::px (36)).reduced (theme::px (8), theme::px (4));
    for (auto* b : { &waveButton, &spectrumButton, &normalise, &gainButton, &silenceButton, &reverseButton, &fadeInButton,
                     &fadeOutButton, &invertButton, &dcButton, &stripButton, &quantiseButton, &zeroButton })
    {
        const int w = b->idealWidth (theme::smallControl);
        b->setVisible (bar.getWidth() >= w);
        b->setBounds (bar.removeFromLeft (w));
        bar.removeFromLeft (theme::px (b == &spectrumButton || b == &normalise || b == &dcButton || b == &quantiseButton ? 12 : 4));
    }
    peaksWidth = 0;
}

void SampleEditor::mouseDown (const MouseEvent& e)
{
    if (! waveArea().contains (e.getPosition())) return;
    dragFrom = sampleAt ((float) e.x);
    selStart = selEnd = -1;
    repaint();
}

void SampleEditor::mouseDrag (const MouseEvent& e)
{
    if (dragFrom < 0) return;
    // The range's ends on zero crossings, when that's on (decision 0054).
    auto onZero = [this] (int at)
    {
        if (! c.zeroCrossings) return at;
        return audiotools::nearestZeroCrossing (audio.getArrayOfReadPointers(), audio.getNumChannels(), audio.getNumSamples(),
                                                at, (int) (rate * 0.01));
    };
    const int s = onZero (sampleAt ((float) e.x)), from = onZero (dragFrom);
    selStart = jmin (from, s);
    selEnd = jmax (from, s);
    repaint();
}

void SampleEditor::mouseUp (const MouseEvent&) { dragFrom = -1; }

} // namespace kal
