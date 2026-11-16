/*
    SampleEditor - an audio clip's file, in the lower zone's Editor (decision 0036).

    The whole file is drawn, the part the clip plays lit, the hits marked.
    Drag across it to choose a range; with nothing chosen, an edit acts on
    the part the clip plays. Edits write a new file and point the clip at it,
    so the original is never changed and Undo goes back.

    Spectrum shows the file as a spectrogram instead: time across, pitch up,
    loudness as brightness.
*/

#pragma once

#include "Controller.h"
#include "Theme.h"

namespace kal
{

class SampleEditor : public juce::Component, private juce::ChangeListener
{
public:
    explicit SampleEditor (Controller&);
    ~SampleEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool spectral = false;
    void load();   // read the chosen clip's file again

private:
    Controller& c;
    theme::HouseButton waveButton { "Waveform", "The file as a waveform.", true };
    theme::HouseButton spectrumButton { "Spectrum", "The file as a spectrogram: pitch up, loudness as brightness.", true };
    theme::HouseButton normalise { "Normalise", "Raise or lower the clip's gain so its loudest peak is at -0.3 dB. The file isn't changed." };
    theme::HouseButton gainButton { ui::u8 ("Gain\xe2\x80\xa6"), "Make the chosen range louder or quieter." };
    theme::HouseButton silenceButton { "Silence", "Make the chosen range silent." };
    theme::HouseButton reverseButton { "Reverse", "Play the chosen range backwards." };
    theme::HouseButton fadeInButton { "Fade In", "Fade the chosen range in." };
    theme::HouseButton fadeOutButton { "Fade Out", "Fade the chosen range out." };
    theme::HouseButton invertButton { "Invert", "Turn the chosen range's waveform upside down (polarity)." };
    theme::HouseButton dcButton { "Remove DC", "Centre the chosen range's waveform on zero." };
    theme::HouseButton stripButton { "Strip Silence", "Cut the clip into one clip for each stretch with sound in it." };
    theme::HouseButton quantiseButton { "Quantise Audio", "Move every hit onto the grid with warp markers." };

    Id loadedClip = 0;
    std::string loadedFile;
    juce::AudioBuffer<float> audio;
    double rate = 48000;
    std::vector<float> peaks;      // two a pixel column, at the last width
    int peaksWidth = 0;
    std::vector<int> hits;         // samples
    juce::Image spectrogram;
    int selStart = -1, selEnd = -1, dragFrom = -1;

    juce::Rectangle<int> waveArea() const;
    int sampleAt (float x) const;
    float xOf (int sample) const;
    void makePeaks();
    void makeSpectrogram();
    // The chosen range, or the part the clip plays.
    std::pair<int, int> range() const;
    void run (const juce::String& label, std::function<void (juce::AudioBuffer<float>&, int, int)>);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
};

} // namespace kal
