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

// A JSFX's shared memory (gmem), as REAPER's gmem_read and gmem_write see it
// (decision 0093): a slot of the memory named `name`, made if need be;
// nullptr while no JSFX using that name is open. Midular's and MIDI
// Production Center's own scripts hand phrases over through it.
double* jsfxGmem (const juce::String& name, unsigned int index);
// The JSFX a device is ("midular"), or empty if it isn't one.
juce::String jsfxIdOf (juce::AudioPluginInstance&);
// A JSFX parameter in its slider's own units, as REAPER's TrackFX_GetParam and
// TrackFX_SetParam take them. False if it isn't a JSFX's slider.
bool jsfxSliderValue (juce::AudioProcessorParameter&, double& value, double& min, double& max);
bool setJsfxSlider (juce::AudioProcessorParameter&, double value);
// What a JSFX exports to the song (REAPER's export_buffer_to_project, as
// Transient Shuffler's Print does), handed to the message thread (0094).
using JsfxExport = std::function<void (juce::AudioPluginInstance&, const juce::AudioBuffer<float>&, double sampleRate)>;
void setJsfxExportHandler (JsfxExport);
// One of a JSFX's own variables, for the tests to look inside it.
double jsfxVariable (juce::AudioPluginInstance&, const char* name);
double jsfxMemory (juce::AudioPluginInstance&, unsigned int address);

// The JSFX a reference names, made; nullptr if it names none.
std::unique_ptr<juce::AudioPluginInstance> makeJsfx (const PluginRef&);

// Instant Virtual Instrument, built in (decision 0067; InstantVirtualInstrument.cpp).
std::unique_ptr<juce::AudioPluginInstance> makeInstantVirtualInstrument();
// Its own demo sound loaded, for the tests (and anyone wanting one to start from).
bool loadInstantVirtualInstrumentDemo (juce::AudioPluginInstance&);
PluginRef instantVirtualInstrumentRef();

// ScaleView, built in (decision 0093; ScaleViewDevice.cpp): the scale, and the chord being played.
std::unique_ptr<juce::AudioPluginInstance> makeScaleView();
PluginRef scaleViewRef();

} // namespace kal
