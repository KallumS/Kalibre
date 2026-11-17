/*
    JsfxDevice - the family's JSFX, built into Kalibre as devices (decision 0066):
    Midi Evolver, Transient Shuffler, Reese Basser, Reverberator and
    Instrument Creator, each the very file REAPER runs (Engines/jsfx/, carried
    inside the app), run by ysfx (REAPER's EEL2 engine), with its own @gfx
    window as its window in the rack.

    Adapted from the wrapper the repos' plugin branches share
    (plugin/source/JsfxPlugin.cpp in Reverberator, Reese Basser and Instrument
    Creator): one host parameter per slider, so automation and last-touched
    work; the sliders and the JSFX's own @serialize data saved with the
    project. What differs per JSFX (its name, kind, window) is a JsfxSpec
    here, not a build of its own; and MIDI the JSFX sends goes on, so Midi
    Evolver plays the instrument after it.
*/

#pragma once

#include "Model.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

namespace kal
{

struct JsfxSpec
{
    const char* id;          // "reese-basser": the identifier is "jsfx:" + this
    const char* name;        // "Reese Basser"
    const char* file;        // in Engines/jsfx/
    DeviceRole role;
    double tailSeconds;
    int minWidth, minHeight; // the window's least size
    juce::uint32 background; // behind the window while it draws
    const char* about;       // a line for the menu's tip
};

const std::vector<JsfxSpec>& jsfxDevices();
PluginRef jsfxRef (const JsfxSpec&);

// The JSFX a reference names, made; nullptr if it names none.
std::unique_ptr<juce::AudioPluginInstance> makeJsfx (const PluginRef&);

} // namespace kal
