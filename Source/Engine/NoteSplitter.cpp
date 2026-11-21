#include "NoteSplitter.h"

namespace kal
{
using namespace juce;

std::function<AudioProcessorEditor* (NoteSplitterProcessor&)> NoteSplitterProcessor::makeEditor;

PluginRef noteSplitterRef()
{
    PluginRef r;
    r.format = "Kalibre";
    r.identifier = "note-splitter";
    r.name = "Note Splitter";
    r.manufacturer = "Kalibre";
    return r;
}

NoteSplitterProcessor::NoteSplitterProcessor() : AudioPluginInstance (BusesProperties())
{
    routed.reserve (4096);
}

void NoteSplitterProcessor::fillInPluginDescription (PluginDescription& d) const
{
    d.name = getName();
    d.pluginFormatName = "Kalibre";
    d.manufacturerName = "Kalibre";
    d.fileOrIdentifier = "note-splitter";
    d.category = "MIDI Effect";
    d.isInstrument = false;
    d.numInputChannels = 0;
    d.numOutputChannels = 0;
}

void NoteSplitterProcessor::prepareToPlay (double r, int)
{
    rate = r > 0 ? r : 48000;
    routed.reserve (4096);
}

void NoteSplitterProcessor::processBlock (AudioBuffer<float>& audio, MidiBuffer& midi)
{
    routed.clear();
    split.setOutputs (outputCount.load());
    // The transport stopping or jumping: every note sent on is ended.
    const auto* head = getPlayHead();
    const auto pos = head != nullptr ? head->getPosition() : std::optional<AudioPlayHead::PositionInfo>();
    const bool playing = pos && pos->getIsPlaying();
    if (wasPlaying && ! playing) split.allOff (0, routed);
    wasPlaying = playing;
    for (const auto m : midi)
        split.add (m.data, m.numBytes, m.samplePosition, (double) (samples + m.samplePosition) / rate, routed);
    split.flush (routed);
    samples += audio.getNumSamples();
    played.fetch_add (1, std::memory_order_release);
    // What came in goes on to this track's own instruments, unchanged.
}

void NoteSplitOutput::processBlock (AudioBuffer<float>& audio, MidiBuffer& midi)
{
    midi.clear();
    audio.clear();
    auto* from = source.load();
    const int k = index.load();
    const auto now = from ? from->played.load (std::memory_order_acquire) : seen;
    if (from == nullptr || now == seen)
    {
        // The splitter didn't play (bypassed, unloaded, removed): what it started, ended.
        for (int key = 0; key < (int) sounding.size(); ++key)
            if (sounding[(size_t) key])
            {
                midi.addEvent (MidiMessage::noteOff (key / 128 + 1, key & 127), 0);
                sounding[(size_t) key] = false;
            }
        return;
    }
    seen = now;
    for (auto& m : from->routed)
    {
        if (m.output != k) continue;
        const int status = m.bytes[0] & 0xF0, key = (m.bytes[0] & 15) * 128 + (m.bytes[1] & 127);
        if (status == 0x90 && m.bytes[2] > 0) sounding[(size_t) key] = true;
        else if (status == 0x80 || status == 0x90) sounding[(size_t) key] = false;
        midi.addEvent (m.bytes, m.size, jlimit (0, jmax (0, audio.getNumSamples() - 1), m.sample));
    }
}

} // namespace kal
