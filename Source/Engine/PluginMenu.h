/*
    PluginMenu - every list of plug-ins to choose from, in three parts (decision
    0131): MIDI Effects, Instruments and Audio Effects, each under its heading
    and by maker, as the device rack's + Add and the browser's Plug-ins tab
    already were (0069, 0125). An instrument that sends MIDI (Riffer, Scaler)
    is under MIDI Effects too. Only the kinds a place takes are listed: the
    Container's effects, a drum pad's instruments.
*/

#pragma once

#include "Model.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <initializer_list>

namespace kal
{

class PluginManager;

namespace pluginmenu
{
// The scanned plug-ins of these kinds; `chosen` gets the one clicked, with the kind it was chosen as.
// Nothing scanned (or none of these kinds): a line saying where to scan.
juce::PopupMenu make (PluginManager&, std::initializer_list<DeviceRole> kinds,
                      std::function<void (const juce::PluginDescription&, DeviceRole)> chosen);
// The heading each kind has.
const char* heading (DeviceRole);

// Kalibre's own devices, in + Add's order, each with its kind (0131). Those
// that hold plug-ins of their own (the Container, Multiband, Stereo Split,
// X/Y Morph, Kalibre Drums, Drum Pads) say so: a slot doesn't offer them.
struct OwnDevice
{
    juce::String name;      // as + Add lists it: "Kalibre Drums (a Kit of Samples and Plug-ins, with Steps)"
    juce::String shortName; // as a slot lists it: "Kalibre Drums"
    PluginRef ref;
    DeviceRole role;
    bool holdsPlugins = false;
};
const std::vector<OwnDevice>& ownDevices();

// A slot that takes a plug-in (a drum pad, an offline effect, the Container,
// Multiband and Stereo Split, an X/Y Morph corner): the scanned plug-ins of
// these kinds, then Kalibre's own (not those that hold plug-ins themselves),
// under "Kalibre". `chosen` gets the one clicked (the musician asked, 0133).
juce::PopupMenu forSlot (PluginManager&, std::initializer_list<DeviceRole> kinds,
                         std::function<void (const PluginRef&, DeviceRole)> chosen);
} // namespace pluginmenu

} // namespace kal
