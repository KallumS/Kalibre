#include "DrumPadsDevice.h"

#include "DrumMachine.h"
#include "Editors.h"
#include "PanLaw.h"
#include "PluginManager.h"

#include <cmath>

namespace kal
{
using namespace juce;

PluginRef drumPadsRef()
{
    PluginRef r;
    r.format = "Kalibre";
    r.identifier = "drum-pads";
    r.name = "Drum Pads";
    r.manufacturer = "Kalibre";
    r.isInstrument = true;
    return r;
}

std::function<void (DrumPadsProcessor&, const String&, std::function<void()>)> DrumPadsProcessor::edit;
std::function<const library::Index*()> DrumPadsProcessor::sounds;
std::function<void (const String&, bool)> DrumPadsProcessor::say;

DrumPadsProcessor::DrumPadsProcessor (PluginManager& pm)
    : AudioPluginInstance (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)), plugins (pm), current (drumpads::standard())
{
    for (auto& v : voices) v.midi.ensureSize (2048);
    for (auto& s : sum) s.assign (8192, 0.0);
    scratch.setSize (4, 8192);
    post();
}

DrumPadsProcessor::~DrumPadsProcessor()
{
    for (auto& p : instances) if (p) editors::close (p->getActiveEditor());
}

void DrumPadsProcessor::fillInPluginDescription (PluginDescription& d) const
{
    d.name = getName();
    d.descriptiveName = "Drum Pads: 128 pads of samples and plug-ins";
    d.pluginFormatName = "Kalibre";
    d.manufacturerName = "Kalibre";
    d.fileOrIdentifier = "drum-pads";
    d.isInstrument = true;
    d.numInputChannels = 0;
    d.numOutputChannels = 2;
}

void DrumPadsProcessor::perform (const String& label, std::function<void()> change)
{
    if (edit) edit (*this, label, std::move (change));
    else change();
}

//==============================================================================
DrumPadsProcessor::Sample DrumPadsProcessor::loadSample (const std::string& path, String& error)
{
    if (path.empty()) return nullptr;
    if (auto it = sampleCache.find (path); it != sampleCache.end()) return it->second;
    Sample s = readDrumSound (path, rate, error);
    if (s) sampleCache[path] = s;
    return s;
}

// The kit as the audio thread plays it, handed over whole.
void DrumPadsProcessor::post()
{
    auto d = std::make_unique<Data>();
    const bool solo = drumpads::anySolo (current);
    for (int i = 0; i < pads; ++i)
    {
        const auto& pad = current.pads[(size_t) i];
        d->samples[(size_t) i] = pad.hasPlugin() ? nullptr : samples[(size_t) i];
        d->plugins[(size_t) i] = instances[(size_t) i];
        const auto [l, r] = panGains ((float) pad.pan);
        const double g = pad.volumeDb == 0.0 ? 1.0 : std::pow (10.0, pad.volumeDb / 20.0);
        d->left[(size_t) i] = g * (double) l;
        d->right[(size_t) i] = g * (double) r;
        d->heard[(size_t) i] = ! pad.mute && (pad.solo || ! solo);
        d->pluginNote[(size_t) i] = pad.pluginNote;
        d->choke[(size_t) i] = pad.choke;
        if (instances[(size_t) i]) d->withPlugins.push_back (i);
    }
    data.post (std::move (d));
}

void DrumPadsProcessor::setKit (drumpads::Kit k, StringArray* problems)
{
    k.view = drumpads::clampView (k.view);
    std::array<Plugin, pads> next;
    std::array<bool, pads> reused {}, taken {};
    // A plug-in already on its pad is kept; one moved to another pad goes with it.
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < pads; ++i)
        {
            const auto& pad = k.pads[(size_t) i];
            if (! pad.hasPlugin() || next[(size_t) i]) continue;
            for (int j = 0; j < pads; ++j)
            {
                if (taken[(size_t) j] || ! instances[(size_t) j] || ! (instanceRefs[(size_t) j] == pad.plugin)) continue;
                if (pass == 0 ? j != i : k.pads[(size_t) j].plugin == instanceRefs[(size_t) j]) continue;
                next[(size_t) i] = instances[(size_t) j];
                taken[(size_t) j] = reused[(size_t) i] = true;
                break;
            }
        }
    std::array<Sample, pads> nextSamples;
    for (int i = 0; i < pads; ++i)
    {
        const auto& pad = k.pads[(size_t) i];
        String error;
        if (pad.hasPlugin())
        {
            if (next[(size_t) i]) continue;
            if (auto inst = plugins.create (pad.plugin, rate, block, error))
            {
                inst->setPlayConfigDetails (inst->getTotalNumInputChannels(), 2, rate, block);
                inst->prepareToPlay (rate, block);
                if (! pad.pluginState.empty())
                {
                    MemoryBlock m;
                    if (m.fromBase64Encoding (pad.pluginState)) inst->setStateInformation (m.getData(), (int) m.getSize());
                }
                next[(size_t) i] = Plugin (inst.release());
            }
            else if (problems) problems->add (String (pad.plugin.name) + " couldn't be loaded: " + error);
        }
        else if (! pad.sample.empty())
        {
            nextSamples[(size_t) i] = loadSample (pad.sample, error);
            if (! nextSamples[(size_t) i] && problems) problems->add (error);
        }
    }
    // Plug-ins let go: their windows closed. The audio thread may still hold them; they go when it's done.
    for (int j = 0; j < pads; ++j)
        if (instances[(size_t) j] && ! taken[(size_t) j]) editors::close (instances[(size_t) j]->getActiveEditor());
    instances = std::move (next);
    for (int i = 0; i < pads; ++i) instanceRefs[(size_t) i] = instances[(size_t) i] ? k.pads[(size_t) i].plugin : PluginRef {};
    samples = std::move (nextSamples);
    current = std::move (k);
    for (auto it = sampleCache.begin(); it != sampleCache.end();)
    {
        bool used = false;
        for (auto& p : current.pads) used |= p.sample == it->first;
        it = used ? std::next (it) : sampleCache.erase (it);
    }
    post();
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    if (onKitChanged) onKitChanged();
}

void DrumPadsProcessor::setView (int firstPad) { current.view = drumpads::clampView (firstPad); }

AudioPluginInstance* DrumPadsProcessor::padPlugin (int i) const { return i >= 0 && i < pads ? instances[(size_t) i].get() : nullptr; }

bool DrumPadsProcessor::padSounds (int i) const
{
    if (i < 0 || i >= pads) return false;
    return current.pads[(size_t) i].hasPlugin() ? instances[(size_t) i] != nullptr : samples[(size_t) i] != nullptr;
}

void DrumPadsProcessor::audition (int pad, int velocity)
{
    auditionVelocity = velocity;
    auditionPad = pad;
}

bool DrumPadsProcessor::takeHit (int pad) { return pad >= 0 && pad < pads && hits[(size_t) pad].exchange (false); }

//==============================================================================
void DrumPadsProcessor::prepareToPlay (double r, int b)
{
    const bool newRate = std::abs (r - rate) > 0.5;
    rate = r;
    block = b;
    fadeLength = jmax (1, (int) std::lround (rate * 0.002));
    const int room = jmax (b, 8192);
    scratch.setSize (4, room);
    for (auto& s : sum) s.assign ((size_t) room, 0.0);
    for (auto& p : instances)
        if (p)
        {
            p->setPlayConfigDetails (p->getTotalNumInputChannels(), 2, rate, block);
            p->prepareToPlay (rate, block);
        }
    if (newRate)
    {
        sampleCache.clear();
        auto k = current;
        setKit (k);   // the samples read again at the new rate
    }
    reset();
}

void DrumPadsProcessor::releaseResources()
{
    for (auto& p : instances) if (p) p->releaseResources();
}

void DrumPadsProcessor::reset()
{
    for (auto& v : voices)
    {
        v.pos = -1;
        v.fade = -1;
        v.offIn = -1;
        v.heldNote = -1;
    }
    for (auto& p : instances) if (p) p->reset();
}

void DrumPadsProcessor::addTrigger (const Trigger& t)
{
    if (triggerCount < (int) triggers.size()) triggers[(size_t) triggerCount++] = t;
}

// The samples playing, from `from` to `to`, added into the 64-bit sums.
void DrumPadsProcessor::render (const Data& d, int from, int to)
{
    if (to <= from) return;
    double* outL = sum[0].data();
    double* outR = sum[1].data();
    for (int i = 0; i < pads; ++i)
    {
        auto& v = voices[(size_t) i];
        const auto* s = d.samples[(size_t) i].get();
        if (v.pos < 0 || s == nullptr) continue;
        const int count = jmin (to - from, s->getNumSamples() - v.pos);
        const float* l = s->getReadPointer (0, v.pos);
        const float* r = s->getReadPointer (s->getNumChannels() > 1 ? 1 : 0, v.pos);
        const double gl = v.gain * d.left[(size_t) i], gr = v.gain * d.right[(size_t) i];
        int k = 0;
        for (; k < count; ++k)
        {
            double fade = 1.0;
            if (v.fade >= 0)
            {
                if (v.fade == 0) break;
                fade = (double) v.fade-- / (double) (fadeLength + 1);
            }
            outL[from + k] += (double) l[k] * gl * fade;
            outR[from + k] += (double) r[k] * gr * fade;
        }
        v.pos += k;
        if (k < count || v.pos >= s->getNumSamples()) { v.pos = -1; v.fade = -1; }
    }
}

// A hit on `pad` stops the others in its choke group: their samples fade, their plug-ins' notes end.
void DrumPadsProcessor::choke (const Data& d, int pad, int at)
{
    const int group = d.choke[(size_t) pad];
    if (group <= 0) return;
    for (int j = 0; j < pads; ++j)
    {
        if (j == pad || d.choke[(size_t) j] != group) continue;
        auto& o = voices[(size_t) j];
        if (o.pos >= 0 && o.fade < 0) o.fade = fadeLength;
        if (o.heldNote >= 0) { o.midi.addEvent (MidiMessage::noteOff (1, o.heldNote), at); o.heldNote = -1; o.offIn = -1; }
    }
}

void DrumPadsProcessor::processBlock (AudioBuffer<float>& audio, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = jmin (audio.getNumSamples(), (int) sum[0].size());
    audio.clear();
    const auto* d = data.take();
    if (d == nullptr || n <= 0) { midi.clear(); return; }
    // A new kit: a pad whose sound changed stops what it played.
    if (d != playing)
    {
        for (int i = 0; i < pads; ++i)
        {
            auto& v = voices[(size_t) i];
            if (v.sound != d->samples[(size_t) i].get()) { v.sound = d->samples[(size_t) i].get(); v.pos = -1; v.fade = -1; }
            if (v.plugin != d->plugins[(size_t) i].get()) { v.plugin = d->plugins[(size_t) i].get(); v.heldNote = -1; v.offIn = -1; }
        }
        playing = d;
    }
    std::fill (sum[0].begin(), sum[0].begin() + n, 0.0);
    std::fill (sum[1].begin(), sum[1].begin() + n, 0.0);

    triggerCount = 0;
    if (const int a = auditionPad.exchange (-1); a >= 0 && a < pads)
        addTrigger ({ 0, a, auditionVelocity.load(), (int) (rate * 0.2), false });
    // Notes played to it: the pad of each note.
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        const int at = jlimit (0, n - 1, m.samplePosition);
        if (msg.isNoteOn()) addTrigger ({ at, msg.getNoteNumber(), msg.getVelocity(), -1, false });
        else if (msg.isNoteOff()) addTrigger ({ at, msg.getNoteNumber(), 0, 0, true });
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            for (int i : d->withPlugins) addTrigger ({ at, i, 0, 0, true });
    }
    midi.clear();
    // In time order: an insertion sort, as stable_sort may allocate (few hits a block).
    for (int i = 1; i < triggerCount; ++i)
        for (int j = i; j > 0 && triggers[(size_t) j - 1].at > triggers[(size_t) j].at; --j)
            std::swap (triggers[(size_t) j - 1], triggers[(size_t) j]);

    // The plug-ins' notes still to end from the block before.
    for (int i : d->withPlugins)
    {
        auto& v = voices[(size_t) i];
        v.midi.clear();
        if (v.offIn >= 0 && v.offIn < n && v.heldNote >= 0) { v.midi.addEvent (MidiMessage::noteOff (1, v.heldNote), v.offIn); v.heldNote = -1; v.offIn = -1; }
        else if (v.offIn >= n) v.offIn -= n;
    }
    // Each hit in turn: the samples played up to it, then the pad started and its choke group stopped.
    int at = 0;
    for (int t = 0; t < triggerCount; ++t)
    {
        const auto& tr = triggers[(size_t) t];
        auto& v = voices[(size_t) tr.pad];
        if (tr.off)
        {
            if (v.heldNote >= 0 && v.offIn < 0) { v.midi.addEvent (MidiMessage::noteOff (1, v.heldNote), tr.at); v.heldNote = -1; }
            continue;
        }
        if (! d->heard[(size_t) tr.pad]) continue;
        render (*d, at, tr.at);
        at = tr.at;
        choke (*d, tr.pad, tr.at);
        hits[(size_t) tr.pad] = true;
        if (auto* plug = d->plugins[(size_t) tr.pad].get(); plug != nullptr)
        {
            const int note = d->pluginNote[(size_t) tr.pad];
            if (v.heldNote >= 0) v.midi.addEvent (MidiMessage::noteOff (1, v.heldNote), tr.at);
            v.heldNote = note;
            v.midi.addEvent (MidiMessage::noteOn (1, note, (uint8) jlimit (1, 127, tr.velocity)), tr.at);
            v.offIn = -1;
            if (tr.gate >= 0)
            {
                const int off = tr.at + tr.gate;
                if (off < n) { v.midi.addEvent (MidiMessage::noteOff (1, note), off); v.heldNote = -1; }
                else v.offIn = off - n;
            }
        }
        else if (d->samples[(size_t) tr.pad])
        {
            v.pos = 0;
            v.fade = -1;
            v.gain = (double) jlimit (0, 127, tr.velocity) / 127.0;
        }
    }
    render (*d, at, n);

    // The plug-ins, each on its own, added in.
    for (int i : d->withPlugins)
    {
        auto* plug = d->plugins[(size_t) i].get();
        auto& v = voices[(size_t) i];
        const int channels = jmin (scratch.getNumChannels(), jmax (2, plug->getTotalNumInputChannels(), plug->getTotalNumOutputChannels()));
        AudioBuffer<float> view (scratch.getArrayOfWritePointers(), channels, n);
        view.clear();
        plug->processBlock (view, v.midi);
        if (! d->heard[(size_t) i]) continue;
        const float* l = view.getReadPointer (0);
        const float* r = view.getReadPointer (channels > 1 ? 1 : 0);
        const double gl = d->left[(size_t) i], gr = d->right[(size_t) i];
        for (int k = 0; k < n; ++k)
        {
            sum[0][(size_t) k] += (double) l[k] * gl;
            sum[1][(size_t) k] += (double) r[k] * gr;
        }
    }
    // Rounded to float once.
    for (int c = 0; c < jmin (2, audio.getNumChannels()); ++c)
    {
        float* out = audio.getWritePointer (c);
        for (int k = 0; k < n; ++k) out[k] = (float) sum[(size_t) c][(size_t) k];
    }
}

//==============================================================================
void DrumPadsProcessor::getStateInformation (MemoryBlock& out)
{
    auto k = current;
    for (int i = 0; i < pads; ++i)
        if (auto* plug = instances[(size_t) i].get())
        {
            MemoryBlock m;
            plug->getStateInformation (m);
            k.pads[(size_t) i].pluginState = m.toBase64Encoding().toStdString();
        }
    const auto text = drumpads::toJson (k);
    out.replaceAll (text.data(), text.size());
}

void DrumPadsProcessor::setStateInformation (const void* bytes, int size)
{
    auto k = drumpads::fromJson (std::string (static_cast<const char*> (bytes), (size_t) jmax (0, size)));
    if (! k) return;
    StringArray problems;
    setKit (*k, &problems);
    // A plug-in kept by setKit gets its settings put back too (Undo of a change inside it).
    for (int i = 0; i < pads; ++i)
        if (instances[(size_t) i] && ! k->pads[(size_t) i].pluginState.empty())
        {
            MemoryBlock m;
            if (m.fromBase64Encoding (k->pads[(size_t) i].pluginState)) instances[(size_t) i]->setStateInformation (m.getData(), (int) m.getSize());
        }
    if (! problems.isEmpty() && say) say ("Drum Pads: " + problems.joinIntoString (" "), true);
}

} // namespace kal
