/*
    PreviewPlayer - hearing a sample or a MIDI file from the browser before
    using it (decision 0094).

    The sound is made first, off the audio thread: a sample read and, with
    Sync on and its tempo known, stretched to the song's tempo (Signalsmith
    Stretch, as a warped clip is); a MIDI file played through Kalibre Synth,
    at the song's tempo with Sync on, otherwise at its own. The finished
    sound reaches the audio thread through a Swap, so the audio thread only
    copies. While the song plays, a synced preview starts on the next beat.

    It's its own audio callback beside the engine's, so it's heard whatever
    the song is doing, and never recorded or exported.
*/

#pragma once

#include "Library.h"
#include "Swap.h"
#include "TempoMap.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace kal
{

class Engine;

class PreviewPlayer : public juce::AudioIODeviceCallback
{
public:
    explicit PreviewPlayer (Engine&);
    ~PreviewPlayer() override;

    // Message thread.
    void play (const library::Item&, bool sync, double songBpm);
    void stop();
    bool isPlaying() const { return length.load() > 0 && position.load() < length.load(); }
    double progress() const;                  // 0-1 through what's playing, -1 when nothing is
    juce::String playingPath() const { return path; }

    // The sound a preview plays, at `rate` (tests, and the background job).
    static std::unique_ptr<juce::AudioBuffer<float>> render (const library::Item&, bool sync, double songBpm, double rate);
    static constexpr double longest = 60.0;   // seconds: a long file previews its start

    // How long until the next beat when the song is playing and the preview syncs.
    static double secondsToNextBeat (const Engine&, const TempoMap&);
    std::function<double()> nextBeat;         // set by the window; 0 when not playing or not synced

    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}

private:
    struct Sound
    {
        juce::AudioBuffer<float> buffer;
        int delay = 0;
        int id = 0;
    };
    Engine& engine;
    Swap<Sound> swap;
    juce::ThreadPool pool { 1 };
    std::atomic<int> requested { 0 };
    int playingId = -1;                        // audio thread
    int pos = 0;                               // audio thread
    std::atomic<int> position { 0 }, length { 0 };
    std::atomic<double> deviceRate { 48000 };
    juce::String path;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

} // namespace kal
