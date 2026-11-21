#include "DrumMachine.h"

#include "Editors.h"
#include "Ideas.h"
#include "PluginManager.h"
#include "Theme.h"

#include <cmath>

namespace kal
{
using namespace juce;

PluginRef drumsRef()
{
    PluginRef r;
    r.format = "Kalibre";
    r.identifier = "drums";
    r.name = "Kalibre Drums";
    r.manufacturer = "Kalibre";
    r.isInstrument = true;
    return r;
}

std::function<void (DrumMachine&, const String&, std::function<void()>)> DrumMachine::edit;
std::function<const library::Index*()> DrumMachine::sounds;
std::function<void (const String&, bool)> DrumMachine::say;

DrumMachine::DrumMachine (PluginManager& pm)
    : AudioPluginInstance (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)), plugins (pm), current (drumkit::standard())
{
}

DrumMachine::~DrumMachine()
{
    for (auto& p : pads) if (p.plugin) editors::close (p.plugin->getActiveEditor());
}

void DrumMachine::fillInPluginDescription (PluginDescription& d) const
{
    d.name = getName();
    d.descriptiveName = "Kalibre Drums: a kit of samples and plug-ins, and a step pattern";
    d.pluginFormatName = "Kalibre";
    d.manufacturerName = "Kalibre";
    d.fileOrIdentifier = "drums";
    d.isInstrument = true;
    d.numInputChannels = 0;
    d.numOutputChannels = 2;
}

void DrumMachine::perform (const String& label, std::function<void()> change)
{
    if (edit) edit (*this, label, std::move (change));
    else change();
}

//==============================================================================
DrumMachine::Sample DrumMachine::loadSample (const std::string& path, String& error)
{
    if (path.empty()) return nullptr;
    if (auto it = sampleCache.find (path); it != sampleCache.end()) return it->second;
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (File (String (path))));
    if (! r || r->sampleRate <= 0) { error = File (String (path)).getFileName() + " isn't an audio file Kalibre can read."; return nullptr; }
    if (r->lengthInSamples > (int64) r->sampleRate * 30) { error = File (String (path)).getFileName() + " is longer than 30 seconds: a drum is a short sound."; return nullptr; }
    AudioBuffer<float> raw (2, (int) r->lengthInSamples);
    r->read (&raw, 0, raw.getNumSamples(), 0, true, true);
    if (r->numChannels == 1) raw.copyFrom (1, 0, raw, 0, 0, raw.getNumSamples());
    auto out = std::make_shared<AudioBuffer<float>>();
    if (std::abs (r->sampleRate - rate) < 0.5)
        *out = std::move (raw);
    else
    {
        // Read once at the device's rate, so playing is a copy.
        const double ratio = r->sampleRate / rate;
        const int frames = (int) std::ceil (raw.getNumSamples() / ratio);
        out->setSize (2, frames);
        for (int c = 0; c < 2; ++c)
        {
            LagrangeInterpolator interp;
            interp.process (ratio, raw.getReadPointer (c), out->getWritePointer (c), frames, raw.getNumSamples(), 0);
        }
    }
    Sample s = out;
    sampleCache[path] = s;
    return s;
}

void DrumMachine::setKit (drumkit::Kit k, StringArray* problems)
{
    drumkit::matchLanes (k);
    // What each pad needs, made before the audio thread is held up.
    std::array<Sample, drumkit::maxPads> samples;
    std::array<std::unique_ptr<AudioPluginInstance>, drumkit::maxPads> made;
    std::array<bool, drumkit::maxPads> reuse {};
    std::array<int, drumkit::maxPads> from;
    from.fill (-1);
    std::array<bool, drumkit::maxPads> taken {};
    for (size_t i = 0; i < k.pads.size(); ++i)
    {
        auto& pad = k.pads[i];
        String error;
        if (! pad.sample.empty())
        {
            samples[i] = loadSample (pad.sample, error);
            if (! samples[i] && problems) problems->add (error);
        }
        if (pad.hasPlugin())
        {
            // The same plug-in where it was, or moved with its pad: kept, not made again.
            for (size_t j = 0; j < pads.size(); ++j)
                if (! taken[j] && pads[j].plugin && pads[j].pluginRef == pad.plugin && (j == i || from[i] < 0))
                {
                    from[i] = (int) j;
                    if (j == i) break;
                }
            if (from[i] >= 0) { taken[(size_t) from[i]] = true; reuse[i] = true; }
            else
            {
                auto inst = plugins.create (pad.plugin, rate, block, error);
                if (inst)
                {
                    inst->setPlayConfigDetails (inst->getTotalNumInputChannels(), 2, rate, block);
                    inst->prepareToPlay (rate, block);
                    made[i] = std::move (inst);
                }
                else if (problems) problems->add (String (pad.plugin.name) + " couldn't be loaded: " + error);
            }
        }
    }
    std::array<Pad, drumkit::maxPads> next;
    {
        const ScopedLock sl (lock);
        for (size_t i = 0; i < k.pads.size(); ++i)
        {
            auto& n = next[i];
            n.sample = samples[i];
            n.samplePath = k.pads[i].sample;
            n.pluginRef = k.pads[i].plugin;
            if (reuse[i]) n.plugin = std::move (pads[(size_t) from[i]].plugin);
            else n.plugin = std::move (made[i]);
            n.midi.ensureSize (2048);
        }
        std::swap (pads, next);
        current = std::move (k);
    }
    // What isn't used any more goes, off the audio thread.
    for (auto& old : next) if (old.plugin) editors::close (old.plugin->getActiveEditor());
    next = {};
    for (auto it = sampleCache.begin(); it != sampleCache.end();)
    {
        bool used = false;
        for (auto& p : current.pads) used |= p.sample == it->first;
        it = used ? std::next (it) : sampleCache.erase (it);
    }
    // A plug-in's settings, as kept with the kit (on Undo, or opening the song).
    for (size_t i = 0; i < current.pads.size(); ++i)
        if (pads[i].plugin && ! current.pads[i].pluginState.empty() && ! reuse[i])
        {
            MemoryBlock m;
            if (m.fromBase64Encoding (current.pads[i].pluginState)) pads[i].plugin->setStateInformation (m.getData(), (int) m.getSize());
        }
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
    if (onKitChanged) onKitChanged();
}

AudioPluginInstance* DrumMachine::padPlugin (int i) const { return i >= 0 && i < drumkit::maxPads ? pads[(size_t) i].plugin.get() : nullptr; }

bool DrumMachine::padSounds (int i) const
{
    if (i < 0 || i >= (int) current.pads.size()) return false;
    return current.pads[(size_t) i].hasPlugin() ? pads[(size_t) i].plugin != nullptr : pads[(size_t) i].sample != nullptr;
}

void DrumMachine::audition (int pad, int velocity)
{
    auditionVelocity = velocity;
    auditionPad = pad;
}

//==============================================================================
void DrumMachine::prepareToPlay (double r, int b)
{
    const bool newRate = std::abs (r - rate) > 0.5;
    rate = r;
    block = b;
    scratch.setSize (4, jmax (b, 4096));
    if (newRate)
    {
        sampleCache.clear();
        auto k = current;
        for (auto& p : pads) { p.sample = nullptr; p.samplePath.clear(); }
        setKit (k);   // the samples read again at the new rate
    }
    for (auto& p : pads)
        if (p.plugin)
        {
            p.plugin->setPlayConfigDetails (p.plugin->getTotalNumInputChannels(), 2, rate, block);
            p.plugin->prepareToPlay (rate, block);
        }
    reset();
}

void DrumMachine::releaseResources()
{
    for (auto& p : pads) if (p.plugin) p.plugin->releaseResources();
}

void DrumMachine::reset()
{
    for (auto& p : pads)
    {
        p.pos = -1;
        p.offIn = -1;
        p.heldNote = -1;
        if (p.plugin) p.plugin->reset();
    }
    playingStep = -1;
}

void DrumMachine::addTrigger (const Trigger& t)
{
    if (triggerCount < (int) triggers.size()) triggers[(size_t) triggerCount++] = t;
}

// The pattern's hits in this block: the song's position in quarter notes, round
// the loop if it wraps in the block; each hit on its very sample.
void DrumMachine::schedulePattern (int n)
{
    auto* ph = getPlayHead();
    if (ph == nullptr) { playingStep = -1; return; }
    const auto pos = ph->getPosition();
    if (! pos || ! pos->getIsPlaying()) { playingStep = -1; return; }
    const auto q = pos->getPpqPosition();
    const auto bpm = pos->getBpm();
    if (! q || ! bpm || *bpm <= 0) return;
    const auto& g = current.pattern;
    const double len = (double) drumkit::patternLength (current);
    if (len <= 0) return;
    const double perQ = rate * 60.0 / *bpm;
    const int gate = jmax (1, (int) std::lround (g.gate * (double) g.stepTicks / PPQ * perQ));
    constexpr double eps = 0.01;   // ticks: a hit on the edge of two blocks plays once
    auto segment = [&] (double qa, double qb, int base)
    {
        const double a = qa * PPQ - eps, b = qb * PPQ - eps;
        for (auto c = (int64) std::floor (a / len); (double) c * len < b; ++c)
            for (int s = 0; s < g.steps; ++s)
            {
                double t = (double) c * len + (double) s * (double) g.stepTicks;
                if (s % 2 == 1) t += std::round (g.swing * (double) g.stepTicks);
                if (t < a || t >= b) continue;
                const int at = jlimit (0, n - 1, base + (int) std::lround ((t / PPQ - qa) * perQ));
                playingStep = s;
                for (size_t l = 0; l < g.lanes.size() && l < current.pads.size(); ++l)
                {
                    const int v = (size_t) s < g.lanes[l].velocity.size() ? g.lanes[l].velocity[(size_t) s] : 0;
                    if (v > 0 && ! current.pads[l].mute) addTrigger ({ at, (int) l, v, gate, false });
                }
            }
    };
    const double qEnd = *q + n / perQ;
    const auto loop = pos->getLoopPoints();
    if (pos->getIsLooping() && loop && loop->ppqEnd > loop->ppqStart && *q < loop->ppqEnd && qEnd > loop->ppqEnd)
    {
        const int k = (int) std::lround ((loop->ppqEnd - *q) * perQ);
        segment (*q, loop->ppqEnd, 0);
        segment (loop->ppqStart, loop->ppqStart + (qEnd - loop->ppqEnd), k);
    }
    else segment (*q, qEnd, 0);
}

void DrumMachine::renderSamples (AudioBuffer<float>& out, int from, int to)
{
    if (to <= from) return;
    for (size_t i = 0; i < current.pads.size(); ++i)
    {
        auto& p = pads[i];
        if (p.pos < 0 || ! p.sample || current.pads[i].hasPlugin()) continue;
        const auto& pad = current.pads[i];
        const float g = p.gain * Decibels::decibelsToGain ((float) pad.volumeDb);
        const float angle = (float) ((pad.pan + 1.0) * MathConstants<double>::pi / 4.0);   // constant power
        const float gl = g * std::cos (angle) * MathConstants<float>::sqrt2, gr = g * std::sin (angle) * MathConstants<float>::sqrt2;
        const auto& s = *p.sample;
        int at = (int) p.pos;
        const int count = jmin (to - from, s.getNumSamples() - at);
        if (count > 0)
        {
            out.addFrom (0, from, s, 0, at, count, gl);
            out.addFrom (1, from, s, 1, at, count, gr);
        }
        at += jmax (0, count);
        p.pos = at >= s.getNumSamples() ? -1 : (double) at;
    }
}

void DrumMachine::processBlock (AudioBuffer<float>& audio, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    audio.clear();
    const int n = audio.getNumSamples();
    const ScopedTryLock sl (lock);
    if (! sl.isLocked()) { midi.clear(); return; }
    triggerCount = 0;
    const int padCount = (int) current.pads.size();

    if (const int a = auditionPad.exchange (-1); a >= 0 && a < padCount)
        addTrigger ({ 0, a, auditionVelocity.load(), (int) (rate * 0.2), false });
    // Notes played to it: the pads on those notes.
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        const int at = jlimit (0, n - 1, m.samplePosition);
        if (msg.isNoteOn())
        {
            for (int i = 0; i < padCount; ++i)
                if (current.pads[(size_t) i].note == msg.getNoteNumber() && ! current.pads[(size_t) i].mute)
                    addTrigger ({ at, i, msg.getVelocity(), -1, false });
        }
        else if (msg.isNoteOff())
        {
            for (int i = 0; i < padCount; ++i)
                if (current.pads[(size_t) i].note == msg.getNoteNumber()) addTrigger ({ at, i, 0, 0, true });
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            for (int i = 0; i < padCount; ++i) addTrigger ({ at, i, 0, 0, true });
    }
    midi.clear();
    if (current.playsPattern) schedulePattern (n);
    else playingStep = -1;
    // In time order: an insertion sort, as stable_sort may allocate (few hits a block).
    for (int i = 1; i < triggerCount; ++i)
        for (int j = i; j > 0 && triggers[(size_t) j - 1].at > triggers[(size_t) j].at; --j)
            std::swap (triggers[(size_t) j - 1], triggers[(size_t) j]);

    // The plug-ins' notes: each hit, and its note-off after its step (or when the note played in ends).
    for (int i = 0; i < padCount; ++i)
    {
        auto& p = pads[(size_t) i];
        p.midi.clear();
        if (p.offIn >= 0 && p.offIn < n && p.heldNote >= 0) { p.midi.addEvent (MidiMessage::noteOff (1, p.heldNote), p.offIn); p.heldNote = -1; p.offIn = -1; }
        else if (p.offIn >= n) p.offIn -= n;
    }
    for (int t = 0; t < triggerCount; ++t)
    {
        const auto& tr = triggers[(size_t) t];
        auto& p = pads[(size_t) tr.pad];
        const auto& pad = current.pads[(size_t) tr.pad];
        if (tr.off)
        {
            if (p.heldNote >= 0 && p.offIn < 0) { p.midi.addEvent (MidiMessage::noteOff (1, p.heldNote), tr.at); p.heldNote = -1; }
            continue;
        }
        // A choke group: the others in it stop here.
        if (pad.choke > 0)
            for (int j = 0; j < padCount; ++j)
                if (j != tr.pad && current.pads[(size_t) j].choke == pad.choke)
                {
                    auto& o = pads[(size_t) j];
                    if (o.heldNote >= 0) { o.midi.addEvent (MidiMessage::noteOff (1, o.heldNote), tr.at); o.heldNote = -1; o.offIn = -1; }
                }
        if (p.heldNote >= 0) p.midi.addEvent (MidiMessage::noteOff (1, p.heldNote), tr.at);
        p.heldNote = pad.pluginNote;
        p.midi.addEvent (MidiMessage::noteOn (1, pad.pluginNote, (uint8) jlimit (1, 127, tr.velocity)), tr.at);
        if (tr.gate >= 0)
        {
            const int off = tr.at + tr.gate;
            if (off < n) { p.midi.addEvent (MidiMessage::noteOff (1, pad.pluginNote), off); p.heldNote = -1; p.offIn = -1; }
            else p.offIn = off - n;
        }
        else p.offIn = -1;   // played in: its own note-off ends it
    }

    // The samples, a stretch at a time between hits.
    int at = 0;
    for (int t = 0; t < triggerCount; ++t)
    {
        const auto& tr = triggers[(size_t) t];
        if (tr.off) continue;
        renderSamples (audio, at, tr.at);
        at = tr.at;
        const auto& pad = current.pads[(size_t) tr.pad];
        if (pad.choke > 0)
            for (int j = 0; j < padCount; ++j)
                if (j != tr.pad && current.pads[(size_t) j].choke == pad.choke) pads[(size_t) j].pos = -1;
        auto& p = pads[(size_t) tr.pad];
        if (p.sample && ! pad.hasPlugin()) { p.pos = 0; p.gain = (float) tr.velocity / 127.0f; }
    }
    renderSamples (audio, at, n);

    // The plug-ins, each on its own and mixed in.
    for (int i = 0; i < padCount; ++i)
    {
        auto& p = pads[(size_t) i];
        if (! p.plugin || n > scratch.getNumSamples()) continue;
        const auto& pad = current.pads[(size_t) i];
        const int channels = jmin (scratch.getNumChannels(), jmax (2, p.plugin->getTotalNumInputChannels(), p.plugin->getTotalNumOutputChannels()));
        AudioBuffer<float> view (scratch.getArrayOfWritePointers(), channels, n);
        view.clear();
        p.plugin->processBlock (view, p.midi);
        if (pad.mute) continue;
        const float g = Decibels::decibelsToGain ((float) pad.volumeDb);
        const float angle = (float) ((pad.pan + 1.0) * MathConstants<double>::pi / 4.0);
        audio.addFrom (0, 0, view, 0, 0, n, g * std::cos (angle) * MathConstants<float>::sqrt2);
        audio.addFrom (1, 0, view, view.getNumChannels() > 1 ? 1 : 0, 0, n, g * std::sin (angle) * MathConstants<float>::sqrt2);
    }
}

//==============================================================================
void DrumMachine::getStateInformation (MemoryBlock& out)
{
    auto k = current;
    for (size_t i = 0; i < k.pads.size(); ++i)
        if (auto* plug = pads[i].plugin.get())
        {
            MemoryBlock m;
            plug->getStateInformation (m);
            k.pads[i].pluginState = m.toBase64Encoding().toStdString();
        }
    const auto text = drumkit::toJson (k);
    out.replaceAll (text.data(), text.size());
}

void DrumMachine::setStateInformation (const void* data, int size)
{
    auto k = drumkit::fromJson (std::string (static_cast<const char*> (data), (size_t) jmax (0, size)));
    if (! k) return;
    // A plug-in kept by setKit gets its settings put back too (Undo of a change inside it).
    auto kit = *k;
    setKit (kit);
    for (size_t i = 0; i < kit.pads.size(); ++i)
        if (pads[i].plugin && ! kit.pads[i].pluginState.empty())
        {
            MemoryBlock m;
            if (m.fromBase64Encoding (kit.pads[i].pluginState)) pads[i].plugin->setStateInformation (m.getData(), (int) m.getSize());
        }
}

} // namespace kal
