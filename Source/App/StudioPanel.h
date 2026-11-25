/*
    StudioPanel - the Studio window (File > Studio): how this Mac's other gear is
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
    int agentsTop() const { return agentsOn.getY() - juce::roundToInt (theme::px (24) * 1.5f); }   // where AI Agents begins

private:
    Controller& c;
    juce::Array<juce::MidiDeviceInfo> ins, outs;
    juce::OwnedArray<juce::ToggleButton> clockOuts;
    juce::ToggleButton sendClock { "Clock and Song Position" }, sendMtc { "Timecode (MTC)" };
    juce::ComboBox mtcRate, follow, mackieIn, mackieOut, followMtc, huiIn, huiOut;
    juce::ToggleButton sendMmc { "Machine Control (MMC)" }, followMmc { "Obey Machine Control" }, networkMidi { "Network MIDI" };
    bool networkMidiListed = false;   // Kalibre's own session's port in the lists (0129)
    juce::ToggleButton link { "Ableton Link" }, linkStartStop { "Share Start and Stop" };
    juce::ToggleButton oscOn { "OSC" };
    juce::TextEditor oscPort, replyHost, replyPort;
    theme::HouseButton rescan { "Look Again", "Look again for MIDI ports plugged in since." };
    juce::String peers;

    // AI agents through MCP (decision 0099).
    juce::ToggleButton agentsOn { "Let AI Agents Control Kalibre" };
    juce::TextEditor agentsPort;
    theme::HouseButton copyDesktop { "Copy Setup for Claude Desktop",
                                     "Copies what to paste into Claude Desktop's settings (Settings > Developer > Edit Config), so it can reach Kalibre." };
    theme::HouseButton copyCode { "Copy Command for Claude Code", "Copies the command that tells Claude Code how to reach Kalibre." };
    juce::String agentsStatus;
    juce::Rectangle<int> agentsStatusArea;

    void build();
    void store();
    void timerCallback() override;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> headings;
};

} // namespace kal
