/*
    Warp - audio files in memory, and audio warped to the project's tempo
    (decision 0022).

    An audio clip is read whole into memory at the project's sample rate the
    first time it is needed, on a background thread (the clip is silent until
    it is ready). A warped clip is rendered the same way into its own buffer,
    laid out in the project's seconds, so the audio thread only ever copies.

    How a moment in the file maps to the timeline:
      - With warp markers (two or more), straight lines between them.
      - Otherwise from the file's own tempo (sourceBpm), so a 120 BPM loop
        stays in time when the project is at 96 or speeding up.
      - With neither, it plays at its own speed.

    The three ways to stretch:
      - Repitch: read faster or slower, like tape. Pitch follows tempo.
      - Granular: overlapping 50 ms grains, each read from where the
        timeline says; good for drums and textures, keeps transients sharp.
      - Spectral: Signalsmith Stretch, a phase vocoder; best for tonal
        material and mixes, and for transposing without changing length.
*/

#pragma once

#include "Model.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>

namespace kal
{

struct AudioData;

struct CachedAudio
{
    juce::AudioBuffer<float> buffer;   // stereo, at the project's rate
    double sampleRate = 48000;
};

class AudioCache : private juce::AsyncUpdater
{
public:
    AudioCache();
    ~AudioCache() override;

    void setSampleRate (double);

    // The audio for a clip as the engine plays it: warped if it is warped.
    // Nothing until it is ready; `onReady` is called (message thread) when
    // anything finishes, so the engine can pick it up.
    std::shared_ptr<const CachedAudio> forClip (const Clip&, const TempoMap&, const juce::File& projectFolder);

    // The file as it is, at the project's rate (for drawing and for offline effects).
    std::shared_ptr<const CachedAudio> file (const juce::File&);

    std::function<void()> onReady;
    void clear();

    // Reading audio info without loading it.
    static bool describe (const juce::File&, double& seconds, double& sampleRate, int& channels);

    // Exposed for tests: render a warp synchronously.
    static std::shared_ptr<CachedAudio> render (const CachedAudio& source, const Clip&, const TempoMap&, double rate);
    static double sourceSecondsAt (const AudioData&, Tick contentTick);
    // Where in the file the clip's content starts, if it plays the file at its own
    // speed throughout (its tempo the song's): then it is copied, never stretched.
    static std::optional<double> unstretchedStart (const Clip&, const TempoMap&, double rate);

private:
    double rate = 48000;
    juce::ThreadPool pool { 2 };
    juce::CriticalSection lock;
    std::map<juce::String, std::shared_ptr<const CachedAudio>> ready;
    std::map<juce::String, bool> working;

    std::shared_ptr<const CachedAudio> load (const juce::File&);
    void handleAsyncUpdate() override;
};

} // namespace kal
