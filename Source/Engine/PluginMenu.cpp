#include "PluginMenu.h"

#include "PluginManager.h"

#include <map>

namespace kal::pluginmenu
{
using namespace juce;

const char* heading (DeviceRole r)
{
    switch (r)
    {
        case DeviceRole::MidiFx:     return "MIDI Effects";
        case DeviceRole::Instrument: return "Instruments";
        case DeviceRole::AudioFx:    return "Audio Effects";
    }
    return "";
}

PopupMenu make (PluginManager& pm, std::initializer_list<DeviceRole> kinds, std::function<void (const PluginDescription&, DeviceRole)> chosen)
{
    std::map<String, PopupMenu> byMaker[3];
    const auto wants = [&kinds] (DeviceRole r) { for (auto k : kinds) if (k == r) return true; return false; };
    for (auto& d : pm.known().getTypes())
    {
        const auto role = pm.roleOf (d);
        const auto maker = d.manufacturerName.isEmpty() ? String ("Other") : d.manufacturerName;
        const auto label = d.name + "  (" + d.pluginFormatName + ")";
        if (wants (role)) byMaker[(int) role][maker].addItem (label, [chosen, d, role] { chosen (d, role); });
        // An instrument that sends MIDI is a MIDI effect too (0069).
        if (role == DeviceRole::Instrument && pm.sendsMidi (d) && wants (DeviceRole::MidiFx))
            byMaker[(int) DeviceRole::MidiFx][maker].addItem (label, [chosen, d] { chosen (d, DeviceRole::MidiFx); });
    }
    PopupMenu m;
    for (auto role : { DeviceRole::MidiFx, DeviceRole::Instrument, DeviceRole::AudioFx })
    {
        if (! wants (role) || byMaker[(int) role].empty()) continue;
        m.addSectionHeader (heading (role));
        for (auto& [maker, sub] : byMaker[(int) role]) m.addSubMenu (maker, sub);
    }
    if (m.getNumItems() == 0) m.addItem ("Scan for plug-ins first (Kalibre > Plug-in Manager)", false, false, [] {});
    return m;
}

} // namespace kal::pluginmenu
