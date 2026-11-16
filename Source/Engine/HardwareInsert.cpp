#include "HardwareInsert.h"

#include "Theme.h"

namespace kal
{
using namespace juce;

HardwareBus& HardwareBus::get()
{
    static HardwareBus bus;
    return bus;
}

PluginRef hardwareInsertRef()
{
    PluginRef r;
    r.format = "Kalibre";
    r.identifier = "hardware-insert";
    r.name = "Hardware Insert";
    r.manufacturer = "Kalibre";
    return r;
}

static StringArray pairNames()
{
    StringArray s;
    for (int i = 0; i < 16; ++i) s.add (String (i * 2 + 1) + "-" + String (i * 2 + 2));
    return s;
}

HardwareInsert::HardwareInsert()
    : AudioPluginInstance (BusesProperties().withInput ("In", AudioChannelSet::stereo(), true).withOutput ("Out", AudioChannelSet::stereo(), true))
{
    auto add = [this] (std::unique_ptr<RangedAudioParameter> p) { params.push_back (p.get()); addHostedParameter (std::move (p)); };
    add (std::make_unique<AudioParameterChoice> (ParameterID { "send", 1 }, "Send to Outputs", pairNames(), 2));
    add (std::make_unique<AudioParameterChoice> (ParameterID { "return", 1 }, "Return from Inputs", pairNames(), 2));
    add (std::make_unique<AudioParameterInt> (ParameterID { "latency", 1 }, "Round Trip (samples)", 0, 48000, 0));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "gain", 1 }, "Return Gain", NormalisableRange<float> (-24.0f, 24.0f), 0.0f,
                                                AudioParameterFloatAttributes().withLabel (" dB")));
}

void HardwareInsert::fillInPluginDescription (PluginDescription& d) const
{
    d.name = "Hardware Insert";
    d.manufacturerName = "Kalibre";
    d.pluginFormatName = "Kalibre";
    d.fileOrIdentifier = "hardware-insert";
    d.numInputChannels = d.numOutputChannels = 2;
}

int HardwareInsert::pair (int which) const
{
    return roundToInt (params[(size_t) which]->convertFrom0to1 (params[(size_t) which]->getValue()));
}

void HardwareInsert::applyLatency()
{
    const int samples = roundToInt (params[Latency]->convertFrom0to1 (params[Latency]->getValue()));
    if (samples != getLatencySamples()) setLatencySamples (samples);
}

void HardwareInsert::prepareToPlay (double r, int)
{
    rate = r;
    applyLatency();
}

void HardwareInsert::ping()
{
    pingResult = -2;
    pingState = 1;
}

void HardwareInsert::processBlock (AudioBuffer<float>& audio, MidiBuffer& midi)
{
    midi.clear();
    auto& hw = HardwareBus::get();
    const int n = audio.getNumSamples();
    const int outCh = pair (SendPair) * 2, inCh = pair (ReturnPair) * 2;
    if (! hw.live || hw.samples != n || outCh + 1 >= hw.numOutputs || inCh + 1 >= hw.numInputs || hw.inputs == nullptr)
    {
        sampleClock += n;
        return;   // no interface (exporting, or the channels aren't there): the sound passes through
    }
    // Ping: one click out, then listen for a second for it to come back.
    if (pingState.load() == 1)
    {
        audio.clear();
        audio.setSample (0, 0, 0.9f);
        audio.setSample (1, 0, 0.9f);
        pingSent = sampleClock;
        pingState = 2;
    }
    else if (pingState.load() == 2) audio.clear();
    for (int c = 0; c < 2; ++c) hw.sends.addFrom (outCh + c, 0, audio, c, 0, n);
    const float g = Decibels::decibelsToGain (params[InputGain]->convertFrom0to1 (params[InputGain]->getValue()));
    for (int c = 0; c < 2; ++c)
    {
        const float* in = hw.inputs[inCh + c];
        auto* out = audio.getWritePointer (c);
        for (int i = 0; i < n; ++i) out[i] = in ? in[i] * g : 0.0f;
    }
    if (pingState.load() == 2)
    {
        for (int i = 0; i < n; ++i)
            if (std::abs (hw.inputs[inCh] ? hw.inputs[inCh][i] : 0.0f) > 0.1f)
            {
                const int found = (int) (sampleClock + i - pingSent);
                pingResult = found;
                pingState = 0;
                MessageManager::callAsync ([this, found]
                {
                    params[Latency]->setValueNotifyingHost (params[Latency]->convertTo0to1 ((float) found));
                    applyLatency();
                });
                break;
            }
        if (pingState.load() == 2 && sampleClock - pingSent > (int64) rate) { pingResult = -1; pingState = 0; }
        audio.clear();
    }
    sampleClock += n;
}

void HardwareInsert::getStateInformation (MemoryBlock& m)
{
    XmlElement x ("HardwareInsert");
    for (auto* p : params) x.setAttribute (p->getParameterID(), p->getValue());
    copyXmlToBinary (x, m);
}

void HardwareInsert::setStateInformation (const void* d, int size)
{
    if (auto x = getXmlFromBinary (d, size))
        for (auto* p : params)
            if (x->hasAttribute (p->getParameterID())) p->setValueNotifyingHost ((float) x->getDoubleAttribute (p->getParameterID()));
    applyLatency();
}

//==============================================================================
namespace
{
class InsertEditor : public AudioProcessorEditor, private Timer
{
public:
    explicit InsertEditor (HardwareInsert& p) : AudioProcessorEditor (p), proc (p)
    {
        for (int i : { (int) HardwareInsert::SendPair, (int) HardwareInsert::ReturnPair })
        {
            auto* b = choosers.add (new theme::HouseButton ({}, i == 0 ? "The interface's outputs the sound goes out of." : "The inputs it comes back in on."));
            b->onClick = [this, i, b]
            {
                PopupMenu m;
                for (int k = 0; k < 16; ++k)
                    m.addItem (String (k * 2 + 1) + "-" + String (k * 2 + 2), true, proc.pair (i) == k, [this, i, k]
                    {
                        proc.param (i)->setValueNotifyingHost (proc.param (i)->convertTo0to1 ((float) k));
                        refresh();
                    });
                m.showMenuAsync (PopupMenu::Options().withTargetComponent (b));
            };
            addAndMakeVisible (b);
        }
        addAndMakeVisible (pingButton);
        pingButton.onClick = [this] { proc.ping(); startTimerHz (10); refresh(); };
        gain.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        gain.setTextBoxStyle (Slider::TextBoxBelow, false, 64, 14);
        gainAttachment = std::make_unique<SliderParameterAttachment> (*proc.param (HardwareInsert::InputGain), gain);
        addAndMakeVisible (gain);
        setResizable (true, false);
        setResizeLimits (260, 160, 2000, 1200);
        setSize (420, 220);
        refresh();
    }

    void timerCallback() override { refresh(); if (! proc.pinging()) stopTimer(); }

    void refresh()
    {
        choosers[0]->setButtonText ("Out " + String (proc.pair (0) * 2 + 1) + "-" + String (proc.pair (0) * 2 + 2));
        choosers[1]->setButtonText ("In " + String (proc.pair (1) * 2 + 1) + "-" + String (proc.pair (1) * 2 + 2));
        repaint();
    }

    void paint (Graphics& g) override
    {
        g.fillAll (theme::sunken);
        g.setColour (theme::text);
        g.setFont (theme::font());
        const int lat = proc.getLatencySamples();
        String status = proc.pinging() ? String ("Listening for the click") + ui::u8 ("\xe2\x80\xa6")
                      : proc.pingResult.load() == -1 ? String ("No click came back. Check the cables and the channels.")
                      : "Round trip: " + String (lat) + " samples (" + String (lat * 1000.0 / jmax (1.0, proc.getSampleRate()), 1) + " ms)";
        g.drawFittedText (status, getLocalBounds().reduced (12).removeFromBottom (40), Justification::centredLeft, 2);
        g.setColour (theme::textSecondary);
        g.setFont (theme::font (theme::smallPoints));
        g.drawFittedText ("Out to the hardware, back in; Ping measures the delay so everything stays in time.",
                          getLocalBounds().reduced (12).removeFromTop (34), Justification::topLeft, 2);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12).withTrimmedTop (38);
        auto row = r.removeFromTop (theme::px (26));
        for (auto* b : choosers) { b->setBounds (row.removeFromLeft (b->idealWidth (theme::smallControl) + 20)); row.removeFromLeft (8); }
        pingButton.setBounds (row.removeFromLeft (pingButton.idealWidth (theme::smallControl)));
        gain.setBounds (r.removeFromLeft (90).withTrimmedBottom (40));
    }

private:
    HardwareInsert& proc;
    OwnedArray<theme::HouseButton> choosers;
    theme::HouseButton pingButton { "Ping", "Send a click through the hardware and measure how long it takes to come back." };
    Slider gain;
    std::unique_ptr<SliderParameterAttachment> gainAttachment;
};
} // namespace

AudioProcessorEditor* HardwareInsert::createEditor() { return new InsertEditor (*this); }

} // namespace kal
