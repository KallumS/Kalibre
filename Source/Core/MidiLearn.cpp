#include "MidiLearn.h"

#include <algorithm>

namespace kal::midimap
{

bool keyOf (const std::uint8_t b[3], int& channel, int& controller)
{
    const int status = b[0] & 0xF0;
    channel = (b[0] & 0x0F) + 1;
    if (status == 0xB0) { controller = b[1]; return true; }
    if (status == 0xE0) { controller = pitchBend; return true; }
    return false;
}

double normalised (const std::uint8_t b[3])
{
    if ((b[0] & 0xF0) == 0xE0) return (double) ((b[2] << 7) | b[1]) / 16383.0;
    return (double) b[2] / 127.0;
}

int match (const std::vector<MidiMapping>& maps, const std::uint8_t b[3])
{
    int channel = 0, controller = 0;
    if (! keyOf (b, channel, controller)) return -1;
    for (size_t i = 0; i < maps.size(); ++i)
        if (maps[i].controller == controller && (maps[i].channel == 0 || maps[i].channel == channel)) return (int) i;
    return -1;
}

void learn (Project& p, int channel, int controller, Id track, const AutoTarget& target, const std::string& name)
{
    auto& maps = p.midiMappings;
    maps.erase (std::remove_if (maps.begin(), maps.end(), [&] (const MidiMapping& m)
    {
        return (m.controller == controller && m.channel == channel) || (m.track == track && m.target == target);
    }), maps.end());
    maps.push_back ({ channel, controller, track, target, name });
}

} // namespace kal::midimap
