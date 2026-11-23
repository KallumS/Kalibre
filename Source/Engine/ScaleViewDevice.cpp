// ScaleView as a built-in device (decision 0093): its own processor and window
// (ThirdParty/ScaleView), unchanged, in a HostedProcessor, as Instant Virtual
// Instrument is (0067). It passes sound and MIDI through and shows the scale,
// naming the chord being played.
#include "HostedProcessor.h"
#include "JsfxDevice.h"
#include "PluginProcessor.h"

namespace kal
{
std::unique_ptr<juce::AudioPluginInstance> makeScaleView()
{
    juce::PluginDescription d;
    d.name = d.descriptiveName = "ScaleView";
    d.pluginFormatName = "Kalibre";
    d.manufacturerName = "Kalibre";
    d.fileOrIdentifier = "scaleview";
    d.isInstrument = false;
    d.numInputChannels = d.numOutputChannels = 2;
    auto p = std::make_unique<HostedProcessor> (std::make_unique<ScaleViewProcessor>(), d);
    p->passMidiOn = true;   // before the instrument, the notes it reads go on to it
    return p;
}

void startScaleViewOn (juce::AudioProcessor& p, int root, int scale)
{
    if (auto* hosted = dynamic_cast<HostedProcessor*> (&p))
        if (auto* view = dynamic_cast<ScaleViewProcessor*> (&hosted->inner()))
            if (view->getRootIndex() < 0) view->setScale (root, scale);
}

PluginRef scaleViewRef()
{
    PluginRef r;
    r.format = "Kalibre";
    r.identifier = "scaleview";
    r.name = "ScaleView";
    r.manufacturer = "Kalibre";
    return r;
}
} // namespace kal
