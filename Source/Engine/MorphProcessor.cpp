#include "NoteSplitter.h"
#include <array>
#include "MorphProcessor.h"
#include "JsfxDevice.h"
#include "LuaTool/LuaTool.h"

#include "BuiltinInstruments.h"
#include "Editors.h"
#include "ContainerProcessor.h"
#include "MultibandProcessor.h"
#include "DrumMachine.h"
#include "HardwareInsert.h"
#include "Morph.h"
#include "PluginManager.h"
#include "Theme.h"

namespace kal
{
using namespace juce;

std::function<void (std::function<void (const PluginRef&)>)> MorphProcessor::choosePlugin;

PluginRef morphRef()
{
    PluginRef r;
    r.format = "Kalibre";
    r.identifier = "xy-morph";
    r.name = "X/Y Morph";
    r.manufacturer = "Kalibre";
    return r;
}

namespace
{
// An instrument with two stereo outputs: silence on the first, a steady 0.25
// on the second (the tests use it for instruments' further outputs).
class MultiOutTest : public AudioPluginInstance
{
public:
    MultiOutTest()
        : AudioPluginInstance (BusesProperties().withOutput ("Main", AudioChannelSet::stereo(), true)
                                                .withOutput ("Aux", AudioChannelSet::stereo(), false)) {}
    const String getName() const override { return "Multi-Out Test"; }
    void fillInPluginDescription (PluginDescription& d) const override { d.name = getName(); d.pluginFormatName = "Kalibre"; d.isInstrument = true; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (AudioBuffer<float>& b, MidiBuffer& m) override
    {
        m.clear();
        b.clear();
        for (int ch = 2; ch < b.getNumChannels(); ++ch) FloatVectorOperations::fill (b.getWritePointer (ch), 0.25f, b.getNumSamples());
    }
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        for (auto& o : l.outputBuses) if (! o.isDisabled() && o != AudioChannelSet::stereo()) return false;
        return l.inputBuses.isEmpty();
    }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return false; }
    AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}
    void getStateInformation (MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

// A device that only delays the sound by a fixed number of samples and says
// so: the tests use it to hold plug-in delay compensation to account.
class LatencyTest : public AudioPluginInstance
{
public:
    explicit LatencyTest (int samples)
        : AudioPluginInstance (BusesProperties().withInput ("In", AudioChannelSet::stereo()).withOutput ("Out", AudioChannelSet::stereo())),
          delay (samples)
    {
        setLatencySamples (samples);
    }
    const String getName() const override { return "Latency Test"; }
    void fillInPluginDescription (PluginDescription& d) const override { d.name = getName(); d.pluginFormatName = "Kalibre"; }
    void prepareToPlay (double, int) override { line.setSize (2, delay + 1); line.clear(); pos = 0; }
    void releaseResources() override {}
    void processBlock (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const int size = line.getNumSamples();
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                line.setSample (ch, pos, b.getSample (ch, i));
                b.setSample (ch, i, line.getSample (ch, (pos + 1) % size));
            }
            pos = (pos + 1) % size;
        }
    }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return false; }
    AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}
    void getStateInformation (MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    int delay;
    AudioBuffer<float> line;
    int pos = 0;
};

// For the tests: a plug-in that turns the sound down or up by so many dB.
class GainTest : public LatencyTest
{
public:
    explicit GainTest (float db) : LatencyTest (0), gain (Decibels::decibelsToGain (db)) {}
    const String getName() const override { return "Gain Test"; }
    void processBlock (AudioBuffer<float>& b, MidiBuffer& m) override { LatencyTest::processBlock (b, m); b.applyGain (gain); }

private:
    float gain;
};

// A gain with one parameter, read once at the start of each call as most
// plug-ins read theirs: what sample-accurate automation is tested on (0089).
class ParamGainTest : public LatencyTest
{
public:
    ParamGainTest() : LatencyTest (0)
    {
        auto g = std::make_unique<AudioParameterFloat> (ParameterID { "gain", 1 }, "Gain", NormalisableRange<float> (0.0f, 1.0f), 1.0f);   // continuous, not in 0.01 steps
        gain = g.get();
        addHostedParameter (std::move (g));
    }
    const String getName() const override { return "Parameter Gain Test"; }
    void processBlock (AudioBuffer<float>& b, MidiBuffer& m) override { LatencyTest::processBlock (b, m); b.applyGain (gain->get()); }

private:
    AudioParameterFloat* gain = nullptr;
};

// A device that takes a fixed time each block, as a heavy plug-in does: the
// tests see it played on several threads at once (decision 0078).
class BusyTest : public LatencyTest
{
public:
    explicit BusyTest (double ms) : LatencyTest (0), seconds (ms / 1000.0) {}
    const String getName() const override { return "Busy Test"; }
    void processBlock (AudioBuffer<float>& b, MidiBuffer& m) override
    {
        LatencyTest::processBlock (b, m);
        const auto until = Time::getHighResolutionTicks() + Time::secondsToHighResolutionTicks (seconds);
        while (Time::getHighResolutionTicks() < until) {}
    }

private:
    double seconds;
};
} // namespace

namespace
{
// A device with a sidechain input that plays only its sidechain: the tests use
// it to hold sidechain routing to account.
class SidechainTest : public AudioPluginInstance
{
public:
    SidechainTest()
        : AudioPluginInstance (BusesProperties().withInput ("In", AudioChannelSet::stereo())
                                                .withInput ("Sidechain", AudioChannelSet::stereo(), false)
                                                .withOutput ("Out", AudioChannelSet::stereo())) {}
    const String getName() const override { return "Sidechain Test"; }
    void fillInPluginDescription (PluginDescription& d) const override { d.name = getName(); d.pluginFormatName = "Kalibre"; }
    bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (AudioBuffer<float>& b, MidiBuffer&) override
    {
        auto side = getBusBuffer (b, true, 1);
        auto out = getBusBuffer (b, false, 0);
        AudioBuffer<float> copy (2, b.getNumSamples());
        copy.clear();
        for (int ch = 0; ch < jmin (2, side.getNumChannels()); ++ch) copy.copyFrom (ch, 0, side, ch, 0, b.getNumSamples());
        for (int ch = 0; ch < out.getNumChannels(); ++ch) out.copyFrom (ch, 0, copy, jmin (ch, 1), 0, b.getNumSamples());
    }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return false; }
    AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}
    void getStateInformation (MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};
} // namespace

namespace
{
// For the tests: a sloppily edited sample library (decision 0085), Note
// Position & Volume Fixer's made-up one. Each note sounds late by its own
// amount (by key and velocity), its attack rising over a while; keys 64, 67
// and 71 too loud or too quiet. CC7 is channel volume on the General MIDI
// curve, 100 to begin with.
class SloppyTest : public AudioPluginInstance
{
public:
    SloppyTest() : AudioPluginInstance (BusesProperties().withOutput ("Out", AudioChannelSet::stereo())) {}
    static double hash (int n)
    {
        uint32 x = (uint32) n ^ 0x9e3779b9u;
        x *= 0x85ebca6bu; x ^= x >> 13; x *= 0xc2b2ae35u; x ^= x >> 16;
        return x / 4294967296.0;
    }
    static double delayMs (int p, int v) { return 45 + 70 * hash (p * 7 + 1) + (127 - v) * 0.12; }
    static double attackMs (int p) { return 12 + 30 * hash (p * 13 + 5); }
    static double gainDb (int p, int v) { return -34 + 0.25 * v - 0.08 * (p - 60) + (p == 64 ? 5.0 : p == 67 ? -4.0 : p == 71 ? 3.5 : 0.0); }

    const String getName() const override { return "Sloppy Library Test"; }
    void fillInPluginDescription (PluginDescription& d) const override { d.name = getName(); d.pluginFormatName = "Kalibre"; d.isInstrument = true; }
    void prepareToPlay (double r, int) override { rate = r; for (auto& v : voices) v = {}; cc7 = 100; }
    void releaseResources() override {}
    void processBlock (AudioBuffer<float>& b, MidiBuffer& midi) override
    {
        b.clear();
        auto it = midi.begin();
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            for (; it != midi.end() && (*it).samplePosition <= i; ++it)
            {
                const auto m = (*it).getMessage();
                if (m.isNoteOn())
                {
                    for (auto& v : voices)
                        if (! v.on) { v = { true, m.getNoteNumber(), m.getVelocity(), 0, -1 }; break; }
                }
                else if (m.isNoteOff())
                {
                    for (auto& v : voices) if (v.on && v.pitch == m.getNoteNumber() && v.off < 0) v.off = v.age;
                }
                else if (m.isController() && m.getControllerNumber() == 7) cc7 = m.getControllerValue();
                else if (m.isAllNotesOff() || m.isAllSoundOff()) for (auto& v : voices) v = {};
            }
            double out = 0;
            for (auto& v : voices)
            {
                if (! v.on) continue;
                const double t = (v.age - delayMs (v.pitch, v.vel) / 1000 * rate) / rate;
                ++v.age;
                if (t < 0) continue;
                const double A = attackMs (v.pitch) / 1000;
                double env = t < A ? 0.5 * (1 - std::cos (MathConstants<double>::pi * t / A)) : std::exp (-(t - A) * 0.6);
                if (v.off >= 0)
                {
                    const double tr = (double) (v.age - v.off) / rate;
                    env *= std::exp (-tr * 12);
                    if (tr > 0.8) { v.on = false; continue; }
                }
                const double f0 = 440 * std::pow (2.0, (v.pitch - 69) / 12.0);
                double s = 0;
                for (int h = 1; h <= 6 && h * f0 <= rate * 0.45; ++h) s += std::sin (MathConstants<double>::twoPi * h * f0 * t + hash (v.pitch * 31 + h) * 6.283) / h;
                out += std::pow (10.0, gainDb (v.pitch, v.vel) / 20) * env * s * 0.5;
            }
            out *= std::pow (cc7 / 127.0, 2.0);
            b.setSample (0, i, (float) out);
            b.setSample (1, i, (float) out);
        }
    }
    double getTailLengthSeconds() const override { return 1; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return false; }
    AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}
    void getStateInformation (MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    struct Voice { bool on = false; int pitch = 0, vel = 0; int64 age = 0, off = -1; };
    std::array<Voice, 32> voices {};
    double rate = 48000;
    int cc7 = 100;
};

// For the tests: a free-running arpeggiator (decision 0064). Whatever comes
// in is dropped; out comes a C5 every quarter of a second, a tenth long.
class ArpTest : public AudioPluginInstance
{
public:
    ArpTest() : AudioPluginInstance (BusesProperties()) {}
    const String getName() const override { return "Arp Test"; }
    void fillInPluginDescription (PluginDescription& d) const override { d.name = getName(); d.pluginFormatName = "Kalibre"; }
    void prepareToPlay (double r, int) override { rate = r; at = 0; }
    void releaseResources() override {}
    void processBlock (AudioBuffer<float>& b, MidiBuffer& m) override
    {
        m.clear();
        const auto every = (int64) (rate * 0.25), length = (int64) (rate * 0.1);
        for (int i = 0; i < b.getNumSamples(); ++i, ++at)
        {
            if (at % every == 0) m.addEvent (MidiMessage::noteOn (1, 72, (uint8) 100), i);
            if (at % every == length) m.addEvent (MidiMessage::noteOff (1, 72), i);
        }
    }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    bool hasEditor() const override { return false; }
    AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}
    void getStateInformation (MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    double rate = 48000;
    int64 at = 0;
};
} // namespace

std::unique_ptr<AudioPluginInstance> makeBuiltIn (const PluginRef& ref, PluginManager& pm)
{
    if (ref.identifier == "test-arp") return std::make_unique<ArpTest>();
    if (ref.identifier == "test-sloppy") return std::make_unique<SloppyTest>();
    if (ref.identifier == "test-param-gain") return std::make_unique<ParamGainTest>();
    if (ref.identifier == "note-splitter") return std::make_unique<NoteSplitterProcessor>();   // decision 0083
    if (auto j = makeJsfx (ref)) return j;   // the family's JSFX (decision 0066)
    if (ref.identifier == "instant-virtual-instrument") return makeInstantVirtualInstrument();   // decision 0067
    if (ref.identifier == "scaleview") return makeScaleView();   // decision 0093
    if (auto t = makeLuaTool (ref)) return t;   // the family's ReaImGui tools (decision 0068)
    if (ref.identifier == "test-sidechain") return std::make_unique<SidechainTest>();
    if (ref.identifier == "test-multiout") return std::make_unique<MultiOutTest>();
    if (ref.identifier == "xy-morph") return std::make_unique<MorphProcessor> (pm);
    if (ref.identifier == "container") return std::make_unique<ContainerProcessor> (pm);
    if (ref.identifier == "multiband") return std::make_unique<MultibandProcessor> (pm);   // decision 0126
    if (ref.identifier == "drums") return std::make_unique<DrumMachine> (pm);   // decision 0105
    if (auto inst = makeInstrument (ref)) return inst;
    if (ref.identifier == "hardware-insert") return std::make_unique<HardwareInsert>();
    if (String (ref.identifier).startsWith ("test-gain:"))
        return std::make_unique<GainTest> (String (ref.identifier).fromFirstOccurrenceOf (":", false, false).getFloatValue());
    if (String (ref.identifier).startsWith ("test-busy:"))
        return std::make_unique<BusyTest> (String (ref.identifier).fromFirstOccurrenceOf (":", false, false).getDoubleValue());
    if (String (ref.identifier).startsWith ("test-latency:"))
        return std::make_unique<LatencyTest> (String (ref.identifier).fromFirstOccurrenceOf (":", false, false).getIntValue());
    return nullptr;
}

String MorphProcessor::Corner::name() const
{
    switch (kind)
    {
        case Kind::Plugin: return String (ref.name);
        case Kind::Sample: return file.getFileNameWithoutExtension();
        case Kind::Empty:  break;
    }
    return "Empty";
}

MorphProcessor::MorphProcessor (PluginManager& pm)
    : AudioPluginInstance (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                            .withOutput ("Output", AudioChannelSet::stereo(), true)),
      plugins (pm)
{
    auto px = std::make_unique<MorphParameter> ("x", "X", 0.0f);
    auto py = std::make_unique<MorphParameter> ("y", "Y", 1.0f);
    x = px.get();
    y = py.get();
    addHostedParameter (std::move (px));
    addHostedParameter (std::move (py));
}

MorphProcessor::~MorphProcessor()
{
    *alive = false;
    for (auto& w : windows) w.reset();   // the corners' windows go before their plug-ins
}

void MorphProcessor::fillInPluginDescription (PluginDescription& d) const
{
    d.name = "X/Y Morph";
    d.pluginFormatName = "Kalibre";
    d.manufacturerName = "Kalibre";
    d.fileOrIdentifier = "xy-morph";
    d.category = "Effect";
    d.numInputChannels = 2;
    d.numOutputChannels = 2;
}

// A corner's window goes before its plug-in does.
static void dropEditor (MorphProcessor::Corner& c)
{
    if (c.plugin)
        editors::close (c.plugin->getActiveEditor());
}

//==============================================================================
// A corner's plug-in in a window of its own: "X/Y Morph  A: Serum".
namespace
{
class CornerWindow : public DocumentWindow
{
public:
    CornerWindow (const String& title, AudioProcessorEditor* editor, std::function<void()> closed)
        : DocumentWindow (title, theme::ground, DocumentWindow::closeButton), onClosed (std::move (closed))
    {
        setUsingNativeTitleBar (true);
        setContentNonOwned (editor, true);
        content.reset (editor);
        setResizable (editor->isResizable(), false);
        if (auto* k = editor->getConstrainer()) setConstrainer (k);
    }
    ~CornerWindow() override { empty(); }
    void empty() { setConstrainer (nullptr); clearContentComponent(); content.reset(); }   // the plug-in is told first (0056)
    void closeButtonPressed() override { MessageManager::callAsync ([f = onClosed] { if (f) f(); }); }
    std::function<void()> onClosed;

private:
    editors::Owned content;
};
} // namespace

void MorphProcessor::showCornerWindow (int c)
{
    if (c < 0 || c >= 4) return;
    auto* plug = corners[(size_t) c].plugin.get();
    if (plug == nullptr) return;
    if (! windows[(size_t) c])
    {
        auto* editor = editors::open (*plug);
        std::weak_ptr<bool> token = alive;
        auto w = std::make_unique<CornerWindow> ("X/Y Morph  " + String (morph::cornerName (c)) + ": " + corners[(size_t) c].name(), editor,
                                                 [this, token, c] { if (! token.expired()) closeCornerWindow (c); });
        // Each corner a step further along, beside Kalibre's window.
        if (auto* top = TopLevelWindow::getActiveTopLevelWindow())
        {
            const auto at = top->getScreenBounds().getCentre() + Point<int> (40 * c - w->getWidth() / 2, 30 * c - w->getHeight() / 2);
            w->setTopLeftPosition (at);
        }
        else w->centreWithSize (w->getWidth(), w->getHeight());
        windows[(size_t) c] = std::move (w);
    }
    windows[(size_t) c]->setVisible (true);
    windows[(size_t) c]->toFront (true);
    if (onWindowsChanged) onWindowsChanged();
}

void MorphProcessor::closeCornerWindow (int c)
{
    if (c < 0 || c >= 4 || ! windows[(size_t) c]) return;
    // Its plug-in's window goes now; the window itself a little later (0115).
    if (auto* w = dynamic_cast<CornerWindow*> (windows[(size_t) c].get())) { w->onClosed = nullptr; w->setVisible (false); w->empty(); }
    editors::retire (std::move (windows[(size_t) c]));
    if (onWindowsChanged) onWindowsChanged();
}

bool MorphProcessor::loadPlugin (int c, const PluginRef& ref, String& error)
{
    auto instance = plugins.create (ref, rate, block, error);
    if (! instance) return false;
    instance->setPlayConfigDetails (2, 2, rate, block);
    instance->prepareToPlay (rate, block);
    closeCornerWindow (c);
    {
        const ScopedLock sl (cornerLock);
        auto& corner = corners[(size_t) c];
        dropEditor (corner);
        corner.kind = Corner::Kind::Plugin;
        corner.ref = ref;
        corner.plugin = std::move (instance);
        corner.sample.setSize (0, 0);
    }
    updateLatency();
    if (onCornersChanged) onCornersChanged();
    return true;
}

bool MorphProcessor::loadSample (int c, const File& f, String& error)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> reader (fm.createReaderFor (f));
    if (! reader) { error = "That file isn't audio Kalibre can read."; return false; }
    const auto frames = (int) jmin<int64> (reader->lengthInSamples, (int64) (reader->sampleRate * 60));
    AudioBuffer<float> buf (2, frames);
    reader->read (&buf, 0, frames, 0, true, true);
    closeCornerWindow (c);
    {
        const ScopedLock sl (cornerLock);
        auto& corner = corners[(size_t) c];
        dropEditor (corner);
        corner.kind = Corner::Kind::Sample;
        corner.file = f;
        corner.sample = std::move (buf);
        corner.sampleRate = reader->sampleRate;
        corner.plugin.reset();
    }
    updateLatency();
    if (onCornersChanged) onCornersChanged();
    return true;
}

void MorphProcessor::clearCorner (int c)
{
    closeCornerWindow (c);
    {
        const ScopedLock sl (cornerLock);
        dropEditor (corners[(size_t) c]);
        corners[(size_t) c] = Corner();
    }
    updateLatency();
    if (onCornersChanged) onCornersChanged();
}

void MorphProcessor::updateLatency()
{
    int most = 0;
    for (auto& c : corners) if (c.plugin) most = jmax (most, c.plugin->getLatencySamples());
    for (size_t i = 0; i < 4; ++i)
    {
        delayLines[i].setSize (2, jmax (1, most + block + 1));
        delayLines[i].clear();
        delayWrite[i] = 0;
    }
    if (most != getLatencySamples()) setLatencySamples (most);
}

void MorphProcessor::prepareToPlay (double sr, int bs)
{
    rate = sr;
    block = bs;
    scratch.setSize (2, bs);
    sum.setSize (2, bs);
    scratchMidi.ensureSize (2048);
    for (auto& c : corners)
        if (c.plugin)
        {
            c.plugin->setPlayConfigDetails (2, 2, sr, bs);
            c.plugin->prepareToPlay (sr, bs);
        }
    updateLatency();
    for (auto& v : voices) v = Voice();
}

void MorphProcessor::releaseResources()
{
    for (auto& c : corners) if (c.plugin) c.plugin->releaseResources();
}

void MorphProcessor::renderSamples (AudioBuffer<float>& out, const MidiBuffer& midi, const std::array<float, 4>& w)
{
    const int n = out.getNumSamples();
    int sampleIndex = 0;
    auto renderTo = [&] (int end)
    {
        for (auto& v : voices)
        {
            if (v.corner < 0) continue;
            const auto& c = corners[(size_t) v.corner];
            if (c.kind != Corner::Kind::Sample || c.sample.getNumSamples() == 0) { v.corner = -1; continue; }
            const float cw = w[(size_t) v.corner];
            for (int i = sampleIndex; i < end; ++i)
            {
                const auto p = (int) v.pos;
                if (p + 1 >= c.sample.getNumSamples()) { v.corner = -1; break; }
                const auto f = (float) (v.pos - p);
                if (v.releasing) { v.fade -= 1.0f / (float) (0.05 * rate); if (v.fade <= 0) { v.corner = -1; break; } }
                const float g = v.gain * cw * v.fade;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const auto* s = c.sample.getReadPointer (ch);
                    out.addSample (ch, i, g * (s[p] + (s[p + 1] - s[p]) * f));
                }
                v.pos += v.step;
            }
        }
        sampleIndex = end;
    };
    for (const auto m : midi)
    {
        renderTo (jlimit (0, n, m.samplePosition));
        const auto msg = m.getMessage();
        if (msg.isNoteOn())
        {
            for (int c = 0; c < 4; ++c)
            {
                if (corners[(size_t) c].kind != Corner::Kind::Sample) continue;
                auto* v = std::find_if (voices.begin(), voices.end(), [] (const Voice& x) { return x.corner < 0; });
                if (v == voices.end()) v = voices.begin();
                v->corner = c;
                v->note = msg.getNoteNumber();
                v->pos = 0;
                v->step = std::pow (2.0, (msg.getNoteNumber() - 60) / 12.0) * corners[(size_t) c].sampleRate / rate;
                v->gain = msg.getFloatVelocity();
                v->releasing = false;
                v->fade = 1.0f;
            }
        }
        else if (msg.isNoteOff())
        {
            for (auto& v : voices) if (v.note == msg.getNoteNumber()) v.releasing = true;
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            for (auto& v : voices) v.corner = -1;
        }
    }
    renderTo (n);
}

void MorphProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    const ScopedTryLock tl (cornerLock);
    if (! tl.isLocked() || n > scratch.getNumSamples()) { buffer.clear(); return; }

    const auto w = morph::weightsAt (x->get(), y->get());
    sum.clear();
    const int latency = getLatencySamples();
    bool anyPlugin = false, anySample = false;

    for (size_t c = 0; c < 4; ++c)
    {
        auto& corner = corners[c];
        if (corner.kind == Corner::Kind::Sample) anySample = true;
        if (! corner.plugin) continue;
        anyPlugin = true;
        for (int ch = 0; ch < 2; ++ch) scratch.copyFrom (ch, 0, buffer, jmin (ch, buffer.getNumChannels() - 1), 0, n);
        scratchMidi.clear();
        scratchMidi.addEvents (midi, 0, n, 0);
        corner.plugin->processBlock (scratch, scratchMidi);

        // Held back by the difference to the slowest corner.
        const int hold = latency - corner.plugin->getLatencySamples();
        auto& line = delayLines[c];
        const int size = line.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                line.setSample (ch, delayWrite[c], scratch.getSample (ch, i));
                const int r = (delayWrite[c] - hold + size) % size;
                sum.addSample (ch, i, w[c] * line.getSample (ch, r));
            }
            delayWrite[c] = (delayWrite[c] + 1) % size;
        }
    }
    if (anySample) renderSamples (sum, midi, w);

    if (anyPlugin || anySample)
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.copyFrom (ch, 0, sum, jmin (ch, 1), 0, n);
    // With no corners at all, the sound passes through.
    midi.clear();
}

void MorphProcessor::getStateInformation (MemoryBlock& out)
{
    XmlElement xml ("XYMorph");
    xml.setAttribute ("x", (double) x->get());
    xml.setAttribute ("y", (double) y->get());
    for (int c = 0; c < 4; ++c)
    {
        auto* e = xml.createNewChildElement ("Corner");
        const auto& corner = corners[(size_t) c];
        e->setAttribute ("index", c);
        if (corner.kind == Corner::Kind::Plugin && corner.plugin)
        {
            e->setAttribute ("kind", "plugin");
            e->setAttribute ("ref", String (corner.ref.description));
            e->setAttribute ("format", String (corner.ref.format));
            e->setAttribute ("identifier", String (corner.ref.identifier));
            e->setAttribute ("name", String (corner.ref.name));
            e->setAttribute ("uid", corner.ref.uid);
            MemoryBlock state;
            corner.plugin->getStateInformation (state);
            e->setAttribute ("state", state.toBase64Encoding());
        }
        else if (corner.kind == Corner::Kind::Sample)
        {
            e->setAttribute ("kind", "sample");
            e->setAttribute ("file", corner.file.getFullPathName());
        }
    }
    copyXmlToBinary (xml, out);
}

void MorphProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (! xml || ! xml->hasTagName ("XYMorph")) return;
    *x = (float) xml->getDoubleAttribute ("x", 0.0);
    *y = (float) xml->getDoubleAttribute ("y", 1.0);
    for (auto* e : xml->getChildWithTagNameIterator ("Corner"))
    {
        const int c = jlimit (0, 3, e->getIntAttribute ("index"));
        String error;
        const auto kind = e->getStringAttribute ("kind");
        if (kind == "plugin")
        {
            PluginRef r;
            r.description = e->getStringAttribute ("ref").toStdString();
            r.format = e->getStringAttribute ("format").toStdString();
            r.identifier = e->getStringAttribute ("identifier").toStdString();
            r.name = e->getStringAttribute ("name").toStdString();
            r.uid = e->getIntAttribute ("uid");
            if (loadPlugin (c, r, error))
            {
                MemoryBlock state;
                state.fromBase64Encoding (e->getStringAttribute ("state"));
                corners[(size_t) c].plugin->setStateInformation (state.getData(), (int) state.getSize());
            }
            else
            {
                // A missing plug-in keeps its place (VST-Morpher's decision 0010).
                corners[(size_t) c].kind = Corner::Kind::Plugin;
                corners[(size_t) c].ref = r;
            }
        }
        else if (kind == "sample")
            loadSample (c, File (e->getStringAttribute ("file")), error);
    }
}

//==============================================================================
namespace
{
class Pad : public Component, private Timer
{
public:
    explicit Pad (MorphProcessor& p) : proc (p)
    {
        setWantsKeyboardFocus (true);
        setTitle ("X/Y pad");
        setDescription ("Drag the point to morph between the four corners. The arrow keys move it.");
        startTimerHz (30);
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (theme::roll);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (theme::rule);
        for (int i = 1; i < 4; ++i)
        {
            g.drawVerticalLine (roundToInt (r.getX() + r.getWidth() * (float) i / 4.0f), r.getY(), r.getBottom());
            g.drawHorizontalLine (roundToInt (r.getY() + r.getHeight() * (float) i / 4.0f), r.getX(), r.getRight());
        }
        g.setColour (theme::edge);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        const auto w = morph::weightsAt (proc.x->get(), proc.y->get());
        g.setFont (theme::font (theme::smallPoints));
        for (int c = 0; c < 4; ++c)
        {
            const auto pos = morph::cornerPosition (c);
            const auto label = String (morph::cornerName (c)) + "  " + proc.corner (c).name() + "  " + String (roundToInt (w[(size_t) c] * 100)) + "%";
            auto area = Rectangle<float> (r.getWidth() * 0.5f - 8.0f, 18.0f)
                            .withPosition (pos[0] < 0.5f ? r.getX() + 6.0f : r.getCentreX() + 2.0f,
                                           pos[1] > 0.5f ? r.getY() + 4.0f : r.getBottom() - 22.0f);
            g.setColour (theme::text);
            g.drawFittedText (label, area.toNearestInt(), pos[0] < 0.5f ? Justification::centredLeft : Justification::centredRight, 1);
        }
        const auto point = Point<float> (r.getX() + proc.x->get() * r.getWidth(), r.getBottom() - proc.y->get() * r.getHeight());
        g.setColour (dragging ? theme::accent : theme::control);
        g.fillEllipse (Rectangle<float> (16.0f, 16.0f).withCentre (point));
        g.setColour (theme::ink);
        g.drawEllipse (Rectangle<float> (16.0f, 16.0f).withCentre (point), 1.5f);
        if (hasKeyboardFocus (false)) theme::drawFocusRing (g, r.expanded (0.0f), 6.0f);
    }

    void mouseDown (const MouseEvent& e) override
    {
        dragging = true;
        proc.x->beginChangeGesture();
        proc.y->beginChangeGesture();
        mouseDrag (e);
    }

    void mouseDrag (const MouseEvent& e) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        proc.x->setValueNotifyingHost (jlimit (0.0f, 1.0f, ((float) e.x - r.getX()) / r.getWidth()));
        proc.y->setValueNotifyingHost (jlimit (0.0f, 1.0f, (r.getBottom() - (float) e.y) / r.getHeight()));
        repaint();
    }

    void mouseUp (const MouseEvent&) override
    {
        dragging = false;
        proc.x->endChangeGesture();
        proc.y->endChangeGesture();
        repaint();
    }

    bool keyPressed (const KeyPress& k) override
    {
        const float step = k.getModifiers().isShiftDown() ? 0.1f : 0.02f;
        if (k == KeyPress::leftKey)  { proc.x->setValueNotifyingHost (jmax (0.0f, proc.x->get() - step)); return true; }
        if (k == KeyPress::rightKey) { proc.x->setValueNotifyingHost (jmin (1.0f, proc.x->get() + step)); return true; }
        if (k == KeyPress::upKey)    { proc.y->setValueNotifyingHost (jmin (1.0f, proc.y->get() + step)); return true; }
        if (k == KeyPress::downKey)  { proc.y->setValueNotifyingHost (jmax (0.0f, proc.y->get() - step)); return true; }
        return false;
    }

    void timerCallback() override { repaint(); }

    MorphProcessor& proc;
    bool dragging = false;
};

class MorphEditor : public AudioProcessorEditor
{
public:
    explicit MorphEditor (MorphProcessor& p) : AudioProcessorEditor (p), proc (p), pad (p)
    {
        addAndMakeVisible (pad);
        for (int c = 0; c < 4; ++c)
        {
            auto* b = buttons.add (new theme::HouseButton (String (morph::cornerName (c))));
            // A corner with a plug-in: its window. Empty or a sample, or a right-click: what it plays.
            b->onClick = [this, c]
            {
                if (! ModifierKeys::currentModifiers.isPopupMenu() && proc.corner (c).plugin != nullptr) proc.showCornerWindow (c);
                else cornerMenu (c);
            };
            addAndMakeVisible (b);
        }
        proc.onCornersChanged = [sp = SafePointer<MorphEditor> (this)] { if (sp) sp->refresh(); };
        proc.onWindowsChanged = [sp = SafePointer<MorphEditor> (this)] { if (sp) sp->refresh(); };
        setResizable (true, false);
        setSize (360, 300);
        refresh();
    }

    ~MorphEditor() override
    {
        proc.onCornersChanged = nullptr;
        proc.onWindowsChanged = nullptr;
    }

    void cornerMenu (int c)
    {
        PopupMenu m;
        m.addItem (ui::u8 ("Load Plug-in\xe2\x80\xa6"), [this, c]
        {
            if (MorphProcessor::choosePlugin)
                MorphProcessor::choosePlugin ([sp = SafePointer<MorphEditor> (this), c] (const PluginRef& r)
                {
                    String error;
                    // Loaded: its window opens, on its own.
                    if (sp && sp->proc.loadPlugin (c, r, error)) sp->proc.showCornerWindow (c);
                });
        });
        m.addItem (ui::u8 ("Load Sample\xe2\x80\xa6"), [this, c]
        {
            chooser = std::make_unique<FileChooser> ("Choose a sample", File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                                  [sp = SafePointer<MorphEditor> (this), c] (const FileChooser& fc)
            {
                String error;
                if (sp && fc.getResult().existsAsFile()) sp->proc.loadSample (c, fc.getResult(), error);
            });
        });
        m.addItem ("Show Its Window", proc.corner (c).plugin != nullptr, false, [this, c] { proc.showCornerWindow (c); });
        m.addSeparator();
        m.addItem ("Clear", proc.corner (c).kind != MorphProcessor::Corner::Kind::Empty, false, [this, c] { proc.clearCorner (c); });
        m.showMenuAsync (PopupMenu::Options().withTargetComponent (buttons[c]));
    }

    // Each letter lit while its corner's window is open; "A..." when a click asks what it plays.
    void refresh()
    {
        for (int c = 0; c < 4; ++c)
        {
            const auto& corner = proc.corner (c);
            const String name (morph::cornerName (c));
            auto* b = buttons[c];
            const bool plug = corner.plugin != nullptr;
            b->setButtonText (plug ? name : name + ui::u8 ("\xe2\x80\xa6"));
            b->setToggleState (proc.cornerWindowOpen (c), dontSendNotification);
            b->setTip (plug ? "Corner " + name + ": " + corner.name() + ". Click to open its window; right-click to change what it plays."
                            : "Choose what corner " + name + " plays: a plug-in or a sample.");
        }
        repaint();
    }

    void paint (Graphics& g) override { g.fillAll (theme::sunken); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        auto row = r.removeFromBottom (theme::px (theme::controlHeight));
        const int w = (row.getWidth() - 3 * 6) / 4;
        for (int i = 0; i < 4; ++i) { buttons[i]->setBounds (row.removeFromLeft (w)); row.removeFromLeft (6); }
        r.removeFromBottom (8);
        pad.setBounds (r);
    }

    MorphProcessor& proc;
    Pad pad;
    OwnedArray<theme::HouseButton> buttons;
    std::unique_ptr<FileChooser> chooser;
};
} // namespace

AudioProcessorEditor* MorphProcessor::createEditor() { return new MorphEditor (*this); }

} // namespace kal
