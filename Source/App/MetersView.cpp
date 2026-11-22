#include "MetersView.h"

#include "AudioTools.h"
#include "Harmony.h"

namespace kal
{
using namespace juce;

MetersView::MetersView (Controller& ctrl) : c (ctrl)
{
    setTitle ("Meters");
}

void MetersView::timerCallback()
{
    const double rate = jmax (8000.0, c.engine.sampleRate());
    if (auto* f = c.engine.fader (c.project.master.id))
    {
        // Spectrum: 4096 points, mono.
        constexpr int n = 4096;
        std::vector<float> l (n), r (n), mono (n);
        f->recent (l.data(), r.data(), n);
        for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (l[(size_t) i] + r[(size_t) i]);
        const auto db = audiotools::spectrumDb (mono.data(), n);
        const int columns = 256;
        spectrum.assign ((size_t) columns, -120.0f);
        held.resize ((size_t) columns, -120.0f);
        for (int x = 0; x < columns; ++x)
        {
            // 20 Hz to 20 kHz, log; the loudest bin in each column.
            const double f0 = 20.0 * std::pow (1000.0, (double) x / columns), f1 = 20.0 * std::pow (1000.0, (double) (x + 1) / columns);
            const int b0 = jlimit (1, (int) db.size() - 1, (int) (f0 / rate * n)), b1 = jlimit (b0, (int) db.size() - 1, (int) (f1 / rate * n));
            float v = -120.0f;
            for (int b = b0; b <= b1; ++b) v = jmax (v, db[(size_t) b]);
            spectrum[(size_t) x] = v;
            held[(size_t) x] = jmax (v, held[(size_t) x] - 0.6f);
        }
        // Oscilloscope: 40 ms from a rising zero.
        const int span = (int) (rate * 0.04);
        int start = n - span - 1;
        for (int i = n - span - 2; i > n - span - (int) (rate * 0.02) && i > 0; --i)
            if (mono[(size_t) i] <= 0.0f && mono[(size_t) i + 1] > 0.0f) { start = i; break; }
        scopeL.assign (l.begin() + start, l.begin() + start + span);
        scopeR.assign (r.begin() + start, r.begin() + start + span);
        // Phase scope: the last 2048.
        phaseL.assign (l.end() - 2048, l.end());
        phaseR.assign (r.end() - 2048, r.end());
        double lr = 0, ll = 0, rr = 0;
        for (size_t i = 0; i < phaseL.size(); ++i) { lr += phaseL[i] * phaseR[i]; ll += phaseL[i] * phaseL[i]; rr += phaseR[i] * phaseR[i]; }
        correlation = ll > 1e-9 && rr > 1e-9 ? lr / std::sqrt (ll * rr) : 0.0;
    }
    // Tuner: the chosen track's sound.
    tunerHz = 0;
    if (auto* f = c.engine.fader (c.focusTrack))
    {
        constexpr int n = 4096;
        std::vector<float> l (n), r (n), mono (n);
        f->recent (l.data(), r.data(), n);
        for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (l[(size_t) i] + r[(size_t) i]);
        tunerHz = audiotools::pitchOf (mono.data(), n, rate);
        if (tunerHz > 0) audiotools::nearestNote (tunerHz, tunerNote, tunerCents);
    }
    repaint();
}

void MetersView::panel (Graphics& g, Rectangle<float> r, const String& title)
{
    g.setColour (theme::roll);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (theme::textSecondary);
    g.setFont (theme::font (theme::smallPoints, true));
    g.drawText (title, r.reduced (8, 4).removeFromTop (16), Justification::topLeft, false);
}

void MetersView::paint (Graphics& g)
{
    g.fillAll (theme::sunken);
    auto r = getLocalBounds().toFloat().reduced (8);
    auto top = r.removeFromTop (r.getHeight() * 0.55f);
    drawSpectrum (g, top.removeFromLeft (top.getWidth() * 0.62f));
    top.removeFromLeft (8);
    drawPhase (g, top);
    r.removeFromTop (8);
    drawScope (g, r.removeFromLeft (r.getWidth() * 0.62f));
    r.removeFromLeft (8);
    drawTuner (g, r);
}

void MetersView::drawSpectrum (Graphics& g, Rectangle<float> r)
{
    panel (g, r, "Spectrum (master)");
    auto area = r.reduced (10).withTrimmedTop (14).withTrimmedBottom (12);
    g.setFont (theme::font (theme::smallPoints));
    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = area.getX() + (float) (std::log (hz / 20.0) / std::log (1000.0)) * area.getWidth();
        g.setColour (theme::lane);
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
        g.setColour (theme::textSecondary);
        g.drawText (hz >= 1000 ? ui::number (hz / 1000.0, 0) + "k" : String ((int) hz), (int) x - 14, (int) area.getBottom(), 28, 12, Justification::centred, false);
    }
    if (spectrum.empty()) return;
    auto yOf = [&] (float db) { return area.getBottom() - jlimit (0.0f, 1.0f, (db + 90.0f) / 90.0f) * area.getHeight(); };
    Path fill, peak;
    fill.startNewSubPath (area.getX(), area.getBottom());
    for (size_t x = 0; x < spectrum.size(); ++x)
    {
        const float px = area.getX() + (float) x / (float) spectrum.size() * area.getWidth();
        fill.lineTo (px, yOf (spectrum[x]));
        if (x == 0) peak.startNewSubPath (px, yOf (held[x])); else peak.lineTo (px, yOf (held[x]));
    }
    fill.lineTo (area.getRight(), area.getBottom());
    fill.closeSubPath();
    g.setColour (theme::control.withAlpha (0.35f));
    g.fillPath (fill);
    g.setColour (theme::control);
    g.strokePath (peak, PathStrokeType (1.2f));
}

void MetersView::drawScope (Graphics& g, Rectangle<float> r)
{
    panel (g, r, "Oscilloscope (master)");
    auto area = r.reduced (10).withTrimmedTop (14);
    g.setColour (theme::lane);
    g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& s = ch == 0 ? scopeL : scopeR;
        if (s.size() < 2) continue;
        Path p;
        for (size_t i = 0; i < s.size(); ++i)
        {
            const float x = area.getX() + (float) i / (float) (s.size() - 1) * area.getWidth();
            const float y = area.getCentreY() - jlimit (-1.0f, 1.0f, s[i]) * area.getHeight() * 0.48f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (ch == 0 ? theme::control : theme::edge);
        g.strokePath (p, PathStrokeType (1.0f));
    }
}

void MetersView::drawPhase (Graphics& g, Rectangle<float> r)
{
    panel (g, r, "Phase (master)");
    auto area = r.reduced (10).withTrimmedTop (14).withTrimmedBottom (18);
    const float size = jmin (area.getWidth(), area.getHeight());
    auto sq = area.withSizeKeepingCentre (size, size);
    g.setColour (theme::lane);
    g.drawLine (sq.getCentreX(), sq.getY(), sq.getCentreX(), sq.getBottom());
    g.drawLine (sq.getX(), sq.getCentreY(), sq.getRight(), sq.getCentreY());
    g.setColour (theme::control.withAlpha (0.5f));
    for (size_t i = 0; i < phaseL.size(); ++i)
    {
        // Mid up, side across.
        const float side = (phaseR[i] - phaseL[i]) * 0.7071f, mid = (phaseL[i] + phaseR[i]) * 0.7071f;
        g.fillRect (sq.getCentreX() + jlimit (-1.0f, 1.0f, side) * size * 0.48f, sq.getCentreY() - jlimit (-1.0f, 1.0f, mid) * size * 0.48f, 1.5f, 1.5f);
    }
    // Correlation: -1 to +1 along the bottom.
    auto bar = r.reduced (10).removeFromBottom (12);
    g.setColour (theme::lane);
    g.fillRect (bar);
    const float x = bar.getX() + (float) ((correlation + 1.0) * 0.5) * bar.getWidth();
    g.setColour (correlation < 0 ? theme::warn : theme::control);
    g.fillRect (x - 2, bar.getY(), 4.0f, bar.getHeight());
    g.setColour (theme::textSecondary);
    g.setFont (theme::font (theme::smallPoints));
    g.drawText ("-1", bar.translated (0, -14), Justification::centredLeft, false);
    g.drawText ("+1", bar.translated (0, -14), Justification::centredRight, false);
}

void MetersView::drawTuner (Graphics& g, Rectangle<float> r)
{
    const auto* t = c.project.track (c.focusTrack);
    panel (g, r, "Tuner (" + (t ? String (t->name) : String ("no track chosen")) + ")");
    auto area = r.reduced (10).withTrimmedTop (16);
    if (tunerHz <= 0)
    {
        g.setColour (theme::textSecondary);
        g.setFont (theme::font());
        g.drawFittedText ("Play a single note into the chosen track (arm it to hear its input).", area.toNearestInt(), Justification::centred, 3);
        return;
    }
    const bool inTune = std::abs (tunerCents) < 3.0;
    g.setColour (theme::text);
    g.setFont (theme::font (theme::bodyPoints * 2.4f, true));
    const auto name = MidiMessage::getMidiNoteName (tunerNote, true, true, 4);
    g.drawText (name, area.removeFromTop (area.getHeight() * 0.5f), Justification::centred, false);
    g.setFont (theme::font (theme::smallPoints));
    g.setColour (theme::textSecondary);
    g.drawText (String (tunerHz, 1) + " Hz  " + ui::u8 ("\xc2\xb7") + "  " + (tunerCents >= 0 ? "+" : "") + ui::number (tunerCents, 0) + " cents",
                area.removeFromBottom (16), Justification::centred, false);
    // The needle: -50 to +50 cents; in tune is lit.
    auto scale = area.reduced (area.getWidth() * 0.08f, area.getHeight() * 0.25f);
    g.setColour (theme::lane);
    g.fillRect (scale);
    g.setColour (theme::edge);
    g.fillRect (scale.getCentreX() - 1, scale.getY(), 2.0f, scale.getHeight());
    const float x = scale.getCentreX() + (float) jlimit (-50.0, 50.0, tunerCents) / 50.0f * scale.getWidth() * 0.5f;
    g.setColour (inTune ? theme::accent : theme::control);
    g.fillRect (x - 3, scale.getY() - 4, 6.0f, scale.getHeight() + 8);
}

} // namespace kal
