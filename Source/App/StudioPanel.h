/*
    StudioPanel - the browser's Studio tab: how this Mac's other gear is
    connected (decisions 0034, 0035). MIDI clock and timecode out, a clock to
    follow, Ableton Link, a Mackie Control surface's ports, and OSC. Saved
    with the app, so every song uses the same studio.
*/

#pragma once

#include "Controller.h"
#include "Theme.h"

namespace kal
{

class StudioPanel : public juce::Component, private juce::Timer
{
public:
    explicit StudioPanel (Controller&);
    void paint (juce::Graphics&) override;
    void resized() override;
    int preferredHeight() const;

private:
    Controller& c;
    juce::Array<juce::MidiDeviceInfo> ins, outs;
    juce::OwnedArray<juce::ToggleButton> clockOuts;
    juce::ToggleButton sendClock { "Clock and Song Position" }, sendMtc { "Timecode (MTC)" };
    juce::ComboBox mtcRate, follow, mackieIn, mackieOut, followMtc, huiIn, huiOut;
    juce::ToggleButton sendMmc { "Machine Control (MMC)" }, followMmc { "Obey Machine Control" }, networkMidi { "Network MIDI" };
    juce::ToggleButton link { "Ableton Link" }, linkStartStop { "Share Start and Stop" };
    juce::ToggleButton oscOn { "OSC" };
    juce::TextEditor oscPort, replyHost, replyPort;
    theme::HouseButton rescan { "Look Again", "Look again for MIDI ports plugged in since." };
    juce::String peers;

    void build();
    void store();
    void timerCallback() override;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> headings;
};

} // namespace kal
