// Instant Virtual Instrument as a built-in instrument (decision 0067): its own
// processor (ThirdParty/InstantVirtualInstrument), in a HostedProcessor.
#include "HostedProcessor.h"
#include "Plugin/PluginProcessor.h"

namespace kal
{
std::unique_ptr<juce::AudioPluginInstance> makeInstantVirtualInstrument()
{
    juce::PluginDescription d;
    d.name = d.descriptiveName = "Instant Virtual Instrument";
    d.pluginFormatName = "Kalibre";
    d.manufacturerName = "KallumS";
    d.fileOrIdentifier = "instant-virtual-instrument";
    d.isInstrument = true;
    d.numInputChannels = 0;
    d.numOutputChannels = 2;
    return std::make_unique<HostedProcessor> (std::make_unique<IVIProcessor>(), d);
}

bool loadInstantVirtualInstrumentDemo (juce::AudioPluginInstance& instance)
{
    auto* hosted = dynamic_cast<HostedProcessor*> (&instance);
    auto* ivi = hosted ? dynamic_cast<IVIProcessor*> (&hosted->inner()) : nullptr;
    if (! ivi) return false;
    ivi->loadDemo();
    return true;
}
} // namespace kal
