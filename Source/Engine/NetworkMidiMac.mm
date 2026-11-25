#include "NetworkMidi.h"

#import <CoreMIDI/CoreMIDI.h>
#import <CoreMIDI/MIDINetworkSession.h>

namespace kal::networkmidi
{
bool available() { return true; }

bool setEnabled (bool on)
{
    MIDINetworkSession* session = [MIDINetworkSession defaultSession];
    if (session == nil) return false;
    session.enabled = on ? YES : NO;
    // Anyone on the network may join: an iPad or another Mac, without a list to keep.
    if (on) session.connectionPolicy = MIDINetworkConnectionPolicy_Anyone;
    return (session.enabled == YES) == on;
}

juce::String sessionName()
{
    MIDINetworkSession* session = [MIDINetworkSession defaultSession];
    if (session == nil || ! session.enabled) return {};
    return juce::String::fromUTF8 ([session.networkName UTF8String]);
}
} // namespace kal::networkmidi

// The Mac's session is a system port: nothing of Kalibre's own (decision 0129).
namespace kal::networkmidi
{
bool ownPort() { return false; }
juce::String portName() { return {}; }
int peers() { return 0; }
int port() { return 0; }
void setReceiver (std::function<void (const juce::MidiMessage&)>) {}
void sendAt (const juce::MidiMessage&, double) {}
bool setEnabledOn (bool on, int) { return setEnabled (on); }
} // namespace kal::networkmidi
