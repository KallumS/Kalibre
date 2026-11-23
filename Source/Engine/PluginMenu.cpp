#include "PluginMenu.h"

#include "PluginManager.h"
#include "BuiltinInstruments.h"
#include "ContainerProcessor.h"
#include "DrumMachine.h"
#include "DrumPadsDevice.h"
#include "HardwareInsert.h"
#include "JsfxDevice.h"
#include "LuaTool/LuaTool.h"
#include "MorphProcessor.h"
#include "MultibandProcessor.h"
#include "NoteSplitter.h"
#include "StereoSplitProcessor.h"

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

const std::vector<OwnDevice>& ownDevices()
{
    static const std::vector<OwnDevice> all = []
    {
        std::vector<OwnDevice> v;
        auto add = [&v] (const String& name, const String& shortName, PluginRef ref, DeviceRole role, bool holds)
        {
            v.push_back ({ name, shortName, std::move (ref), role, holds });
        };
        add ("Kalibre Synth", "Kalibre Synth", synthRef(), DeviceRole::Instrument, false);
        add ("Kalibre Sampler", "Kalibre Sampler", samplerRef(), DeviceRole::Instrument, false);
        add ("Kalibre Drums (a Kit of Samples and Plug-ins, with Steps)", "Kalibre Drums", drumsRef(), DeviceRole::Instrument, true);
        add ("Drum Pads (128 Pads of Samples and Plug-ins)", "Drum Pads", drumPadsRef(), DeviceRole::Instrument, true);
        add ("Trombolese (a Trombone Crossed with an Oboe)", "Trombolese", tromboleseRef(), DeviceRole::Instrument, false);
        add ("X/Y Morph (Samples or Instruments)", "X/Y Morph", morphRef(), DeviceRole::Instrument, true);
        add ("Instant Virtual Instrument", "Instant Virtual Instrument", instantVirtualInstrumentRef(), DeviceRole::Instrument, false);
        add ("Hardware Insert (Outboard Gear)", "Hardware Insert", hardwareInsertRef(), DeviceRole::AudioFx, false);
        add ("X/Y Morph (Effects)", "X/Y Morph", morphRef(), DeviceRole::AudioFx, true);
        add ("Container (Plug-ins on a Band, the Mid or the Sides)", "Container", containerRef(), DeviceRole::AudioFx, true);
        add ("Multiband (2 to 4 Bands, Plug-ins on Each)", "Multiband", multibandRef(), DeviceRole::AudioFx, true);
        add ("Stereo Split (Left, Middle, Right or Middle and Sides, Plug-ins on Each)", "Stereo Split", stereoSplitRef(), DeviceRole::AudioFx, true);
        add ("Note Splitter (a Chord's Notes to Several Tracks)", "Note Splitter", noteSplitterRef(), DeviceRole::MidiFx, false);
        // ScaleView reads what's played: before the instrument, where the notes are (decision 0093).
        add ("ScaleView", "ScaleView", scaleViewRef(), DeviceRole::MidiFx, false);
        // The family's JSFX (decision 0066) and ReaImGui tools (0068), built in, each in its part.
        for (auto& s : jsfxDevices()) add (s.name, s.name, jsfxRef (s), s.role, false);
        for (auto& s : luaTools()) add (s.name, s.name, luaToolRef (s), DeviceRole::MidiFx, false);
        return v;
    }();
    return all;
}

PopupMenu forSlot (PluginManager& pm, std::initializer_list<DeviceRole> kinds, std::function<void (const PluginRef&, DeviceRole)> chosen)
{
    PopupMenu m;
    const auto wants = [&kinds] (DeviceRole r) { for (auto k : kinds) if (k == r) return true; return false; };
    // The scanned ones, in their three parts, by maker.
    if (! pm.known().getTypes().isEmpty())
    {
        auto scanned = make (pm, kinds, [chosen] (const PluginDescription& d, DeviceRole r) { chosen (PluginManager::refFor (d), r); });
        bool any = false;
        for (PopupMenu::MenuItemIterator it (scanned); it.next();) any = any || it.getItem().isEnabled;
        if (any) for (PopupMenu::MenuItemIterator it (scanned); it.next();) m.addItem (it.getItem());
    }
    // Kalibre's own, in the same parts, under one heading.
    for (auto role : { DeviceRole::MidiFx, DeviceRole::Instrument, DeviceRole::AudioFx })
    {
        if (! wants (role)) continue;
        PopupMenu own;
        for (auto& d : ownDevices())
            if (d.role == role && ! d.holdsPlugins && ! (role == DeviceRole::AudioFx && d.ref.identifier == hardwareInsertRef().identifier))
                own.addItem (d.shortName, [chosen, ref = d.ref, role] { chosen (ref, role); });
        if (own.getNumItems() == 0) continue;
        m.addSectionHeader (String ("Kalibre: ") + heading (role));
        for (PopupMenu::MenuItemIterator it (own); it.next();) m.addItem (it.getItem());
    }
    return m;
}

} // namespace kal::pluginmenu
