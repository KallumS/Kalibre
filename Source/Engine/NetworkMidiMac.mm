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
