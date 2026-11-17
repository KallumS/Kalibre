#include "HostedProcessor.h"

namespace kal
{
using namespace juce;

AudioProcessor::BusesProperties HostedProcessor::busesOf (const AudioProcessor& p)
{
    BusesProperties b;
    for (int i = 0; i < p.getBusCount (true); ++i)
        if (auto* bus = p.getBus (true, i)) b.addBus (true, bus->getName(), bus->getLastEnabledLayout(), bus->isEnabled());
    for (int i = 0; i < p.getBusCount (false); ++i)
        if (auto* bus = p.getBus (false, i)) b.addBus (false, bus->getName(), bus->getLastEnabledLayout(), bus->isEnabled());
    return b;
}

namespace
{
// The plug-in's own parameter, passed on: what the host sets reaches it, and
// what its window moves reaches the host (automation, last touched).
class ProxyParameter : public HostedAudioProcessorParameter, private AudioProcessorParameter::Listener
{
public:
    explicit ProxyParameter (AudioProcessorParameter& p) : target (p) { target.addListener (this); }
    ~ProxyParameter() override { target.removeListener (this); }

    float getValue() const override { return target.getValue(); }
    // As a plug-in format's wrapper does: set, then tell its listeners (its own
    // saved-state tree among them), which this one isn't told back.
    void setValue (float v) override
    {
        forwarding = true;
        target.setValue (v);
        target.sendValueChangedMessageToListeners (v);
        forwarding = false;
    }
    float getDefaultValue() const override { return target.getDefaultValue(); }
    String getName (int max) const override { return target.getName (max); }
    String getLabel() const override { return target.getLabel(); }
    int getNumSteps() const override { return target.getNumSteps(); }
    bool isDiscrete() const override { return target.isDiscrete(); }
    bool isBoolean() const override { return target.isBoolean(); }
    String getText (float v, int max) const override { return target.getText (v, max); }
    float getValueForText (const String& t) const override { return target.getValueForText (t); }
    bool isAutomatable() const override { return target.isAutomatable(); }
    String getParameterID() const override
    {
        if (auto* withId = dynamic_cast<const HostedAudioProcessorParameter*> (&target)) return withId->getParameterID();
        if (auto* withId = dynamic_cast<const AudioProcessorParameterWithID*> (&target)) return withId->paramID;
        return "p" + String (target.getParameterIndex());
    }

private:
    AudioProcessorParameter& target;
    bool forwarding = false;

    void parameterValueChanged (int, float v) override { if (! forwarding) sendValueChangedMessageToListeners (v); }
    void parameterGestureChanged (int, bool starting) override { if (starting) beginChangeGesture(); else endChangeGesture(); }
};

// The device's window, holding the plug-in's own and closing it properly.
class HostedEditor : public AudioProcessorEditor, private ComponentListener
{
public:
    explicit HostedEditor (HostedProcessor& owner) : AudioProcessorEditor (owner)
    {
        inner.reset (owner.inner().createEditorAndMakeActive());
        if (! inner) inner.reset (new GenericAudioProcessorEditor (owner.inner()));
        addAndMakeVisible (*inner);
        inner->addComponentListener (this);
        setResizable (inner->isResizable(), false);
        if (auto* k = inner->getConstrainer())
            setResizeLimits (k->getMinimumWidth(), k->getMinimumHeight(), k->getMaximumWidth(), k->getMaximumHeight());
        setSize (inner->getWidth(), inner->getHeight());
    }

    ~HostedEditor() override
    {
        inner->removeComponentListener (this);
        auto* e = inner.release();
        e->processor.editorBeingDeleted (e);   // the plug-in told first (decision 0056)
        delete e;
    }

    void resized() override
    {
        laying = true;
        inner->setBounds (getLocalBounds());
        laying = false;
    }

private:
    std::unique_ptr<AudioProcessorEditor> inner;
    bool laying = false;

    // The plug-in resized its own window: the device's follows.
    void componentMovedOrResized (Component& c, bool, bool wasResized) override
    {
        if (! laying && wasResized && &c == inner.get()) setSize (inner->getWidth(), inner->getHeight());
    }
};
} // namespace

HostedProcessor::HostedProcessor (std::unique_ptr<AudioProcessor> p, PluginDescription d)
    : AudioPluginInstance (busesOf (*p)), processor (std::move (p)), description (std::move (d))
{
    for (auto* param : processor->getParameters()) addHostedParameter (std::make_unique<ProxyParameter> (*param));
    setLatencySamples (processor->getLatencySamples());
}

HostedProcessor::~HostedProcessor() = default;   // its window is closed before it goes (decision 0056)

void HostedProcessor::prepareToPlay (double rate, int block)
{
    processor->setRateAndBufferSizeDetails (rate, block);
    processor->prepareToPlay (rate, block);
    setLatencySamples (processor->getLatencySamples());
}

void HostedProcessor::processBlock (AudioBuffer<float>& audio, MidiBuffer& midi)
{
    processor->processBlock (audio, midi);
}

AudioProcessorEditor* HostedProcessor::createEditor() { return new HostedEditor (*this); }

} // namespace kal
