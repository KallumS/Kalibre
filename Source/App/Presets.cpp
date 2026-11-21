/*
    The Controller's presets and A/B (decision 0039).

    A preset is the plug-in's own saved state in a small file, kept per
    plug-in in Application Support/Kalibre/Presets/<maker>/<plug-in>/, so it
    works the same for AU, VST3, CLAP and Kalibre's own. A plug-in's factory
    programs are listed beside them.

    A/B: a device holds two settings, the one heard and the other; switching
    swaps them, so you can compare a change against how it was.
*/

#include "Controller.h"
#include "RandomSong.h"
#include "Theme.h"

namespace kal
{
using namespace juce;

static String safeName (const std::string& s)
{
    auto n = File::createLegalFileName (String (s)).trim();
    return n.isEmpty() ? String ("Unknown") : n;
}

File Controller::presetsFolder (const PluginRef& r) const
{
    return scriptsFolder().getParentDirectory().getChildFile ("Presets")
               .getChildFile (safeName (r.manufacturer)).getChildFile (safeName (r.name));
}

Array<File> Controller::presetsFor (const PluginRef& r) const
{
    auto files = presetsFolder (r).findChildFiles (File::findFiles, true, "*.kpreset");
    files.sort();
    return files;
}

bool Controller::savePreset (Id deviceId, const String& name)
{
    const auto* d = project.device (deviceId);
    if (! d) return false;
    const auto state = engine.stateOf (deviceId);
    if (state.empty()) { say ("The plug-in has to be on to save a preset."); return false; }
    auto folder = presetsFolder (d->plugin);
    folder.createDirectory();
    DynamicObject::Ptr o = new DynamicObject();
    o->setProperty ("kalibrePreset", 1);
    o->setProperty ("plugin", String (d->plugin.name));
    o->setProperty ("format", String (d->plugin.format));
    o->setProperty ("state", String (state));
    const auto f = folder.getChildFile (File::createLegalFileName (name) + ".kpreset");
    if (! f.replaceWithText (JSON::toString (var (o.get())))) { say ("The preset couldn't be saved.", true); return false; }
    say ("Saved the preset " + name.quoted() + ".");
    return true;
}

bool Controller::loadPreset (Id deviceId, const File& f)
{
    auto* inst = engine.instance (deviceId);
    if (! inst) return false;
    const auto v = JSON::parse (f.loadFileAsString());
    MemoryBlock m;
    if (! m.fromBase64Encoding (v.getProperty ("state", {}).toString())) { say ("That preset can't be read.", true); return false; }
    storeDeviceStates();
    const Project before = project;
    inst->setStateInformation (m.getData(), (int) m.getSize());
    changedDevice = deviceId;
    pluginChanged (before, "Preset " + f.getFileNameWithoutExtension());
    say ("Loaded " + f.getFileNameWithoutExtension().quoted() + ".", false, true);
    return true;
}

void Controller::loadProgram (Id deviceId, int index)
{
    auto* inst = engine.instance (deviceId);
    if (! inst || index < 0 || index >= inst->getNumPrograms()) return;
    storeDeviceStates();
    const Project before = project;
    inst->setCurrentProgram (index);
    changedDevice = deviceId;
    pluginChanged (before, "Program " + inst->getProgramName (index));
}

void Controller::switchAB (Id deviceId)
{
    storeDeviceStates();
    auto* d = project.device (deviceId);
    auto* inst = engine.instance (deviceId);
    if (! d || ! inst) return;
    const Project before = project;
    const auto now = d->state;
    const auto other = d->stateB.empty() ? now : d->stateB;
    MemoryBlock m;
    if (m.fromBase64Encoding (other)) inst->setStateInformation (m.getData(), (int) m.getSize());
    d->stateB = now;
    d->onB = ! d->onB;
    changedDevice = deviceId;
    pluginChanged (before, d->onB ? "Switch to B" : "Switch to A");
}

void Controller::copyToOther (Id deviceId)
{
    storeDeviceStates();
    const auto* d = project.device (deviceId);
    if (! d) return;
    const auto now = d->state;
    const bool onB = d->onB;
    apply (onB ? "Copy B to A" : "Copy A to B", [deviceId, now] (Project& p) { if (auto* x = p.device (deviceId)) x->stateB = now; });
    say (String (onB ? "A" : "B") + " is now the same as " + (onB ? "B" : "A") + ".");
}

String randomVerdict (const PluginManager::Control& c, const String& plugin)
{
    using C = AudioProcessorParameter::Category;
    if (c.bypass) return "not sound";
    if (c.meta || ! c.automatable) return "not sound";                    // a host can't set it, or it sets others
    if (c.category == (int) C::inputGain || c.category == (int) C::outputGain) return "volume";
    if (c.category != (int) C::genericParameter) return "not sound";     // meters only read
    return String (randomsong::whyKept (c.name.toStdString(), c.unit.toStdString(), plugin.toStdString()));
}

String Controller::whyNoRandom (Id deviceId) const
{
    auto in = [deviceId] (const Track& t) -> String
    {
        for (auto& d : t.devices) if (d.id == deviceId) return String (randomsong::whyNoRandom (d.plugin.name));
        return {};
    };
    for (auto& t : project.tracks) if (auto w = in (t); w.isNotEmpty()) return w;
    for (auto& t : project.cue) if (auto w = in (t); w.isNotEmpty()) return w;
    return in (project.master);
}

int Controller::randomSettings (Id deviceId, unsigned seed)
{
    storeDeviceStates();
    auto* d = project.device (deviceId);
    auto* inst = engine.instance (deviceId);
    if (! d || ! inst) { say ("Switch the plug-in on first: its controls are set by chance while it's loaded.", true); return 0; }
    if (const auto why = randomsong::whyNoRandom (d->plugin.name); ! why.empty())
    {
        say (String (d->plugin.name) + " is " + String (why) + ": its controls are all volume, tuning or pitch, so Random leaves it alone.", true);
        return 0;
    }
    const Project before = project;
    Random rng (seed != 0 ? (int64) seed : Time::currentTimeMillis());
    int changed = 0, kept = 0;
    const ScopedValueSetter<bool> quiet (settingByChance, true);   // not touches: no automation written, nothing "last touched"
    const auto controls = PluginManager::controlsOf (*inst);
    const auto params = inst->getParameters();
    for (int i = 0; i < params.size() && i < (int) controls.size(); ++i)
    {
        if (randomVerdict (controls[(size_t) i], d->plugin.name).isNotEmpty()) { ++kept; continue; }
        params[i]->setValueNotifyingHost (rng.nextFloat());
        ++changed;
    }
    if (changed == 0) { say (String (d->plugin.name) + " has no controls chance may change.", true); return 0; }
    changedDevice = deviceId;
    pluginChanged (before, "Random Settings");
    say (String (d->plugin.name) + ": " + String (changed) + (changed == 1 ? " control" : " controls") + " set by chance"
         + (kept > 0 ? String ("; ") + String (kept) + " for volume, tuning and pitch (and settings that aren't the sound) left as they were."
                     : String (".")), false, true);
    return changed;
}

// Written into a std::string, which grows by doubling: a juce::String grows only
// as far as each append needs, so hundreds of thousands of lines were copied
// whole at every one, and Kalibre froze at the end of a big library's report.
String randomReportText (const std::vector<PluginManager::PluginControls>& all, int* pluginCount, int* controlCount)
{
    std::string text;
    size_t lines = 0;
    for (auto& pc : all) lines += pc.controls.size() + 2;
    text.reserve (lines * 48 + 512);
    auto add = [&text] (const String& s) { text += s.toStdString(); };
    add ("Kalibre: Random Settings Report, " + Time::getCurrentTime().toString (true, true) + "\n\n"
         "Every control of every plug-in Kalibre has scanned, and whether a plug-in's Random button changes it\n"
         "(decision 0104). \"random\": chance may change it. \"kept\": it's left as it is, and why.\n\n");
    int plugins = 0, random = 0, keptCount = 0;
    for (auto& pc : all)
    {
        ++plugins;
        add ("== " + pc.type.name + " (" + pc.type.manufacturerName + ", " + pc.type.pluginFormatName + ")");
        if (const auto why = randomsong::whyNoRandom (pc.type.name.toStdString()); ! why.empty())
        {
            text += ": no Random, " + why + " ==\n\n";
            continue;
        }
        if (pc.controls.empty()) { add (": " + (pc.problem.isNotEmpty() ? pc.problem : String ("no controls")) + " ==\n\n"); continue; }
        text += " ==\n";
        for (auto& ctl : pc.controls)
        {
            const auto why = randomVerdict (ctl, pc.type.name);
            (why.isEmpty() ? random : keptCount) += 1;
            std::string label = why.isEmpty() ? std::string ("random") : "kept: " + why.toStdString();
            if (label.size() < 18) label.append (18 - label.size(), ' ');
            text += "  " + label + ctl.name.toStdString();
            if (ctl.unit.trim().isNotEmpty()) text += " (" + ctl.unit.trim().toStdString() + ")";
            text += "\n";
        }
        text += "\n";
    }
    text += "In all: " + std::to_string (plugins) + " plug-ins, " + std::to_string (random + keptCount) + " controls, "
          + std::to_string (random) + " random, " + std::to_string (keptCount) + " kept.\n";
    if (pluginCount) *pluginCount = plugins;
    if (controlCount) *controlCount = random + keptCount;
    return String::fromUTF8 (text.data(), (int) text.size());
}

void Controller::randomSettingsReport (const File& out)
{
    auto& pm = engine.plugins();
    if (pm.readingControls() || pm.scanning()) { say ("Wait for the plug-ins being looked at now, then ask again."); return; }
    say ("Reading every plug-in's controls for the Random Settings Report" + ui::u8 ("\xe2\x80\xa6") + " Each opens in the helper, so this takes a few minutes.");
    auto progress = [this] (float p, const String& what)
    {
        if (p < 1.0f) say ("Random Settings Report, " + String (roundToInt (p * 100)) + "%: reading " + what + ui::u8 ("\xe2\x80\xa6"));
    };
    pm.readControls (progress, [this, out] (std::vector<PluginManager::PluginControls> all)
    {
        say ("Writing the Random Settings Report" + ui::u8 ("\xe2\x80\xa6"));
        // Written on a thread of its own: a big library's report is millions of characters.
        auto shared = std::make_shared<std::vector<PluginManager::PluginControls>> (std::move (all));
        Thread::launch ([shared, out, alive = std::weak_ptr<bool> (alive), this]
        {
            int plugins = 0, controls = 0;
            const auto text = randomReportText (*shared, &plugins, &controls);
            const bool written = out.replaceWithText (text);
            MessageManager::callAsync ([=]
            {
                if (alive.expired()) return;
                if (! written) { say ("The Random Settings Report couldn't be written to " + out.getFullPathName() + ".", true); return; }
                out.revealToUser();
                say ("The Random Settings Report is on your Desktop: " + out.getFileName() + " (" + String (plugins) + " plug-ins, "
                     + String (controls) + " controls).");
            });
        });
    });
}

// A plug-in's own state was changed by Kalibre: one undo step, which puts
// the old state back into the plug-in (Engine::sync reloads it on undo).
void Controller::pluginChanged (const Project& before, const String& label)
{
    storeDeviceStates();
    if (auto* d = project.device (changedDevice)) ++d->stateVersion;
    history.push (before, label.toStdString());
    dirty = true;
    refresh();
}

} // namespace kal
