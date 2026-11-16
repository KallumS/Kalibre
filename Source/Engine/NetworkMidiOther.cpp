#include "NetworkMidi.h"

namespace kal::networkmidi
{
bool available() { return false; }
bool setEnabled (bool on) { return ! on; }
juce::String sessionName() { return {}; }
} // namespace kal::networkmidi
