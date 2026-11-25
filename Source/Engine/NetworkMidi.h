/*
    NetworkMidi - a network MIDI session, switched on from Kalibre's Studio
    window (decision 0043), so an iPad or another computer can play into it
    and follow its clock without setting anything else up.

    On the Mac it's the Mac's own session: once on, it's a MIDI port like any
    other ("Network Session 1"). On Windows (and Linux) Kalibre holds the
    session itself (decision 0129), speaking RTP-MIDI as the Mac's does, and
    announces it on the network (on Windows, through Windows' own DNS-SD), so
    a Mac's Audio MIDI Setup or an iPad finds "Kalibre" and connects. There
    it's one port of Kalibre's own, in the Studio window's lists:
    `portId`, named `portName()`.
*/

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <functional>

namespace kal::networkmidi
{
bool available();
bool setEnabled (bool);          // true when the session is now as asked
juce::String sessionName();      // what other computers see, or empty

// Off the Mac: Kalibre's own session as a port. On the Mac these do nothing: its session is a system port.
inline constexpr const char* portId = "kalibre-network-session";
bool ownPort();                  // true off the Mac, while the session is on
juce::String portName();         // "Network Session (Kalibre)"
int peers();                     // computers connected now
int port();                      // the session's control port (its data port is the next), 0 when off
// What's played in, on the session's own thread.
void setReceiver (std::function<void (const juce::MidiMessage&)>);
// Sent to every computer connected, at a time on Time::getMillisecondCounterHiRes's clock.
// Any thread, the audio thread too: no lock, no allocation.
void sendAt (const juce::MidiMessage&, double whenMs);
// Tests: the session on these ports (0: the usual, 5004 or the next free pair).
bool setEnabledOn (bool, int firstPort);
} // namespace kal::networkmidi
