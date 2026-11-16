/*
    MetersView - the analysers, in the lower zone's Meters tab (decision 0044).

      - Spectrum: the master's frequencies, 20 Hz to 20 kHz on a log scale,
        with the peak held a moment.
      - Oscilloscope: the master's waveform, steadied on a rising zero.
      - Phase scope: left against right (mid up, side across) with the
        correlation under it: a mono sound is a vertical line, out-of-phase
        sound spreads sideways.
      - Tuner: the chosen track's note and how many cents off it is: play
        into an armed track to tune a guitar.
*/

#pragma once

#include "Controller.h"
#include "Theme.h"

namespace kal
{

class MetersView : public juce::Component, private juce::Timer
{
public:
    explicit MetersView (Controller&);
    void paint (juce::Graphics&) override;
    void visibilityChanged() override { if (isVisible()) startTimerHz (30); else stopTimer(); }

private:
    Controller& c;
    std::vector<float> spectrum, held;   // dB per display column
    std::vector<float> scopeL, scopeR, phaseL, phaseR;
    double tunerHz = 0;
    int tunerNote = -1;
    double tunerCents = 0;
    double correlation = 0;

    void timerCallback() override;
    void drawSpectrum (juce::Graphics&, juce::Rectangle<float>);
    void drawScope (juce::Graphics&, juce::Rectangle<float>);
    void drawPhase (juce::Graphics&, juce::Rectangle<float>);
    void drawTuner (juce::Graphics&, juce::Rectangle<float>);
    void panel (juce::Graphics&, juce::Rectangle<float>, const juce::String& title);
};

} // namespace kal
