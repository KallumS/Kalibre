/*
    NetworkMidi - the Mac's own network MIDI session, switched on from
    Kalibre's Studio tab (decision 0043), so an iPad or another Mac can play
    into it and follow its clock without opening Audio MIDI Setup. Once on,
    the session is a MIDI port like any other ("Network Session 1").

    Elsewhere than the Mac, it says it isn't there.
*/

#pragma once

#include <juce_core/juce_core.h>

namespace kal::networkmidi
{
bool available();
bool setEnabled (bool);          // true when the session is now as asked
juce::String sessionName();      // what other computers see, or empty
} // namespace kal::networkmidi
