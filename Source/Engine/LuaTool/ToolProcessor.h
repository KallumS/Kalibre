// The processor behind a Lua tool device (LuaTool.h, decision 0068).
#pragma once

#include "LuaTool.h"
#include "Swap.h"

#include <array>
#include <atomic>
#include <map>

namespace kal::tools
{

class ToolSession;

struct BlockNote { double start = 0, len = 0; int pitch = 60, vel = 100; };   // quarter notes

// The block, handed whole to the audio thread.
struct PlayBlock
{
    std::vector<BlockNote> notes;   // sorted by start
    double beats = 0;
    int version = 0;
};

// Notes sounding from one source (the song, or the audition).
struct Voices
{
    struct V { int pitch; double off; };
    std::array<V, 512> v {};
    int n = 0;
};

class ToolProcessor : public juce::AudioProcessor
{
public:
    explicit ToolProcessor (const LuaToolSpec&);
    ~ToolProcessor() override;

    const LuaToolSpec& spec;
    juce::AudioPluginInstance* owner = nullptr;   // the device wrapping this one

    void prepareToPlay (double rate, int) override { sampleRate = rate; out.ensureSize (8192); }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l.getMainInputChannelSet() == l.getMainOutputChannelSet(); }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return spec.name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    ToolSession* session() { return current.get(); }

    // ---- what the window script reaches through `host` (message thread) ----
    void setBlock (std::vector<BlockNote>, double beats);
    std::atomic<bool> withSong { false }, loop { false };
    void audition (bool on);
    bool auditioning() const { return auditionOn.load(); }
    double auditionFraction() const;
    double tempo() const { return songTempo.load(); }
    double cursor() const { return songPosition.load(); }
    int sigNum() const { return songNum.load(); }
    int sigDen() const { return songDen.load(); }

    std::map<juce::String, juce::String> extState();
    void setExt (const juce::String& key, const juce::String& value);

    int editorWidth = 0, editorHeight = 0;

private:
    void restartSession();
    void play (Voices&, const PlayBlock&, double from, double to, double spb, int sample0, int numSamples, bool repeat, juce::MidiBuffer&);
    void release (Voices&, double upTo, double from, double spb, int sample0, juce::MidiBuffer&);
    void releaseAll (Voices&, int sample, juce::MidiBuffer&);

    std::unique_ptr<ToolSession> current;

    juce::CriticalSection extLock;   // message thread only
    std::map<juce::String, juce::String> ext;

    Swap<PlayBlock> block;
    int blockVersion = 0;
    std::unique_ptr<juce::Timer> collector;

    // audio thread
    double sampleRate = 44100;
    Voices songVoices, auditionVoices;
    juce::MidiBuffer out;
    double lastSongEnd = -1;
    int songVersion = -1;
    double auditionAt = 0;
    std::atomic<bool> auditionOn { false };
    std::atomic<int> auditionRequest { 0 };   // 1 start, 2 stop
    std::atomic<double> auditionShown { 0 }, auditionBeats { 0 };
    std::atomic<double> songTempo { 120 }, songPosition { 0 };
    std::atomic<int> songNum { 4 }, songDen { 4 };
};

} // namespace kal::tools
