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
} // namespace pluginmenu

} // namespace kal
