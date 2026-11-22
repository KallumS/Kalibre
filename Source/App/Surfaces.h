/*
    Surfaces - control surfaces (decision 0035).

    Mackie Control: eight motorised faders on the tracks (and the master),
    V-Pots for pan, Record, Solo, Mute and Select on each strip, the
    transport, banks of eight, the jog wheel, the display showing names and
    levels, the timecode display showing bars and beats, and meters. The
    surface's ports are chosen in File > Studio.

    OSC, for TouchOSC, Lemur, Open Stage Control or a script: Kalibre listens
    on a port and answers on another. The addresses are in docs/CONTROL.md.

    Everything a surface does goes through the controller like a click would;
    a fader move is one undo step from touch to release.
*/

#pragma once

#include "Controller.h"
#include "Hui.h"
#include "McpServer.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_osc/juce_osc.h>

namespace kal
{

class Surfaces : private juce::Timer,
                 private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    explicit Surfaces (Controller&);
    ~Surfaces() override;

    void configure();   // from the controller's studio settings

    // What a surface did, as the controller sees it; public for the tests.
    void mackie (const juce::MidiMessage&);
    void hui (const juce::MidiMessage&);       // a HUI surface (decision 0043)
    void osc (const juce::OSCMessage&);
    std::vector<Id> strips() const;   // the tracks on the eight strips now
    int bank = 0;

    // What would be sent to the Mackie surface (the tests read it; otherwise it goes out).
    std::function<void (const juce::MidiMessage&)> sendToSurface;
    std::function<void (const juce::MidiMessage&)> sendToHui;

    // AI agents through MCP (decision 0099), on and off with the studio's settings.
    McpServer& agents() { return mcp; }

private:
    Controller& c;
    McpServer mcp { c };
    juce::SpinLock inputLock;
    juce::String mackieIn;
    std::unique_ptr<juce::MidiOutput> mackieOut;
    juce::String huiIn;
    std::unique_ptr<juce::MidiOutput> huiOut;
    hui::Decoder huiDecoder;
    std::array<bool, 8> huiTouched {};
    int huiPlayShown = -1, huiStopShown = -1, huiRecordShown = -1, huiPingCountdown = 0;
    void huiSend (const std::uint8_t* data, int size);
    void huiFeedback (bool everything);
    std::array<bool, 9> touched {};
    Project touchBefore;

    struct Shown
    {
        int fader = -1, pan = -1, meter = -1;
        int arm = -1, solo = -1, mute = -1, select = -1;
        std::string name, value;
    };
    std::array<Shown, 9> shown;
    std::array<Shown, 8> huiShown;
    int playShown = -1, recordShown = -1, cycleShown = -1;
    std::string timeShown;

    std::unique_ptr<juce::OSCReceiver> oscIn;
    juce::OSCSender oscOut;
    bool oscConnected = false;
    std::map<juce::String, juce::String> oscShown;

    void send (const juce::MidiMessage&);
    void sendBytes (const std::uint8_t* data, int size);
    void feedback (bool everything);
    void oscFeedback();
    void timerCallback() override;
    void oscMessageReceived (const juce::OSCMessage& m) override { osc (m); }
    Track* stripTrack (int strip);
};

} // namespace kal
