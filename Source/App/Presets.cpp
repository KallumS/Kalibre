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
