#include "Warp.h"

#include "AudioTools.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

namespace kal
{
using namespace juce;

AudioCache::AudioCache() = default;

AudioCache::~AudioCache()
{
    pool.removeAllJobs (true, 10000);
}

void AudioCache::setSampleRate (double r)
{
    if (std::abs (r - rate) < 0.5) return;
    rate = r;
    clear();
}

void AudioCache::clear()
{
    const ScopedLock sl (lock);
    ready.clear();
}

bool AudioCache::describe (const File& f, double& seconds, double& sampleRate, int& channels)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
    if (! r || r->sampleRate <= 0) return false;
    seconds = (double) r->lengthInSamples / r->sampleRate;
    sampleRate = r->sampleRate;
    channels = (int) r->numChannels;
    return true;
}

std::shared_ptr<const CachedAudio> AudioCache::load (const File& f)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
    if (! r || r->sampleRate <= 0) return nullptr;
    const auto frames = (int) jmin<int64> (r->lengthInSamples, (int64) (r->sampleRate * 60 * 30));
    AudioBuffer<float> raw (2, frames);
    r->read (&raw, 0, frames, 0, true, r->numChannels > 1);
    if (r->numChannels == 1) raw.copyFrom (1, 0, raw, 0, 0, frames);

    auto out = std::make_shared<CachedAudio>();
    out->sampleRate = rate;
    if (std::abs (r->sampleRate - rate) < 0.5)
        out->buffer = std::move (raw);
    else
    {
        const double ratio = r->sampleRate / rate;
        const int outFrames = (int) std::ceil (frames / ratio);
        out->buffer.setSize (2, outFrames);
        for (int c = 0; c < 2; ++c)
        {
            LagrangeInterpolator interp;
            interp.process (ratio, raw.getReadPointer (c), out->buffer.getWritePointer (c), outFrames, frames, 0);
        }
    }
    return out;
}

std::shared_ptr<const CachedAudio> AudioCache::file (const File& f)
{
    const auto key = "file:" + f.getFullPathName() + "@" + String (rate);
    {
        const ScopedLock sl (lock);
        if (auto it = ready.find (key); it != ready.end()) return it->second;
    }
    auto loaded = load (f);
    const ScopedLock sl (lock);
    if (loaded) ready[key] = loaded;
    return loaded;
}

//==============================================================================
double AudioCache::sourceSecondsAt (const AudioData& a, Tick t)
{
    return audiotools::sourceSecondsAt (a, t);
}

std::optional<double> AudioCache::unstretchedStart (const Clip& clip, const TempoMap& tempo, double rate)
{
    const auto& a = clip.audio;
    const Tick contentStart = clip.start - clip.offset;
    const Tick contentLength = clip.loopLength > 0 ? clip.loopLength : clip.offset + clip.length;
    const double t0 = tempo.secondsAt (contentStart);
    // How far the file is ahead of the song, at a moment of the clip's content.
    auto lead = [&] (Tick t)
    {
        const double s = sourceSecondsAt (a, t);
        const double song = tempo.secondsAt (contentStart + t) - t0;
        return (s < 0 ? a.sourceStart + song : s) - song;
    };
    // Where either can bend: the ends, the markers, the tempo's changes; and halfway between.
    std::vector<Tick> at { 0, contentLength };
    for (auto& m : a.markers) if (m.tick > 0 && m.tick < contentLength) at.push_back (m.tick);
    for (auto& tp : tempo.tempos()) if (tp.tick > contentStart && tp.tick < contentStart + contentLength) at.push_back (tp.tick - contentStart);
    std::sort (at.begin(), at.end());
    at.erase (std::unique (at.begin(), at.end()), at.end());
    const size_t ends = at.size();
    for (size_t i = 0; i + 1 < ends; ++i) at.push_back ((at[i] + at[i + 1]) / 2);
    const double first = lead (0);
    for (Tick t : at)
        if (std::abs (lead (t) - first) * rate > 0.01) return std::nullopt;
    return first;
}

std::shared_ptr<CachedAudio> AudioCache::render (const CachedAudio& src, const Clip& clip, const TempoMap& tempo, double rate)
{
    auto out = std::make_shared<CachedAudio>();
    out->sampleRate = rate;
    const auto& a = clip.audio;
    const Tick contentStart = clip.start - clip.offset;
    const Tick contentLength = clip.loopLength > 0 ? clip.loopLength : clip.offset + clip.length;
    const double t0 = tempo.secondsAt (contentStart);
    const double seconds = tempo.secondsAt (contentStart + contentLength) - t0;
    const int frames = jmax (1, (int) std::ceil (seconds * rate));
    out->buffer.setSize (2, frames);
    out->buffer.clear();

    AudioBuffer<float> source (src.buffer);
    if (a.reversed) source.reverse (0, source.getNumSamples());
    const int srcFrames = source.getNumSamples();

    // Where in the file each output frame reads from, in the file's frames.
    auto sourceFrameAt = [&] (int frame) -> double
    {
        const double ticks = tempo.ticksAt (t0 + frame / rate) - (double) contentStart;
        const double s = sourceSecondsAt (a, (Tick) std::llround (ticks));
        return (s < 0 ? a.sourceStart + frame / rate : s) * rate;
    };

    // At its own tempo the clip plays unstretched in every warp mode (the audio
    // engine's document): the file's own samples, copied, never through a
    // stretcher, so a clip whose tempo is the song's is neutral, and is again
    // whenever the tempo comes back to it.
    if (std::abs (a.transpose) < 1e-9)
        if (const auto start = unstretchedStart (clip, tempo, rate))
        {
            const auto first = (long long) std::llround (*start * rate);
            for (int c = 0; c < 2; ++c)
            {
                const auto* s = source.getReadPointer (c);
                auto* o = out->buffer.getWritePointer (c);
                for (int i = 0; i < frames; ++i)
                {
                    const long long p = first + i;
                    if (p >= 0 && p < srcFrames) o[i] = s[p];
                }
            }
            return out;
        }

    switch (a.warp)
    {
        case WarpMode::Off:
        case WarpMode::Repitch:
        {
            for (int i = 0; i < frames; ++i)
            {
                const double pos = a.warp == WarpMode::Off ? a.sourceStart * rate + i : sourceFrameAt (i);
                const int p = (int) pos;
                if (p < 0 || p + 1 >= srcFrames) continue;
                const float f = (float) (pos - p);
                for (int c = 0; c < 2; ++c)
                {
                    const auto* s = source.getReadPointer (c);
                    out->buffer.setSample (c, i, s[p] + (s[p + 1] - s[p]) * f);
                }
            }
            break;
        }
        case WarpMode::Granular:
        {
            // 50 ms Hann grains every 25 ms: the windows sum to one.
            const int grain = jmax (64, (int) (0.05 * rate));
            const int hop = grain / 2;
            const double pitch = std::pow (2.0, a.transpose / 12.0);
            std::vector<float> window ((size_t) grain);
            for (int i = 0; i < grain; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (MathConstants<float>::twoPi * (float) i / (float) grain);
            for (int start = -hop; start < frames; start += hop)
            {
                const int centre = jlimit (0, frames - 1, start + grain / 2);
                const double from = sourceFrameAt (centre) - grain / 2 * pitch;
                for (int i = 0; i < grain; ++i)
                {
                    const int o = start + i;
                    if (o < 0 || o >= frames) continue;
                    const double pos = from + i * pitch;
                    const int p = (int) pos;
                    if (p < 0 || p + 1 >= srcFrames) continue;
                    const float f = (float) (pos - p);
                    for (int c = 0; c < 2; ++c)
                    {
                        const auto* s = source.getReadPointer (c);
                        out->buffer.addSample (c, o, window[(size_t) i] * (s[p] + (s[p + 1] - s[p]) * f));
                    }
                }
            }
            break;
        }
        case WarpMode::Spectral:
        {
            signalsmith::stretch::SignalsmithStretch<float> stretch;
            stretch.presetDefault (2, (float) rate);
            stretch.setTransposeSemitones ((float) a.transpose);
            // Chunks of a sixteenth note, each stretched by its own ratio, so
            // tempo ramps and warp markers are followed as they go.
            const int chunk = jmax (256, (int) (rate * 0.03));
            std::vector<std::vector<float>> in (2), outBuf (2, std::vector<float> ((size_t) chunk));
            const int latencyIn = stretch.inputLatency(), latencyOut = stretch.outputLatency();
            // Prime with the audio just before the start so the first chunk isn't late.
            double readPos = sourceFrameAt (0);
            {
                std::vector<std::vector<float>> pre (2, std::vector<float> ((size_t) latencyIn, 0.0f));
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < latencyIn; ++i)
                    {
                        const int p = (int) readPos - latencyIn + i;
                        pre[(size_t) c][(size_t) i] = (p >= 0 && p < srcFrames) ? source.getSample (c, p) : 0.0f;
                    }
                stretch.seek (pre, latencyIn, 1.0);
            }
            std::vector<std::vector<float>> skip (2, std::vector<float> ((size_t) jmax (1, latencyOut)));
            int written = -latencyOut;
            for (int o = 0; o < frames + latencyOut; o += chunk)
            {
                const double next = sourceFrameAt (jmin (frames - 1, o + chunk));
                const int inCount = jmax (1, (int) std::llround (next - readPos));
                for (int c = 0; c < 2; ++c)
                {
                    in[(size_t) c].resize ((size_t) inCount);
                    for (int i = 0; i < inCount; ++i)
                    {
                        const int p = (int) readPos + i;
                        in[(size_t) c][(size_t) i] = (p >= 0 && p < srcFrames) ? source.getSample (c, p) : 0.0f;
                    }
                }
                readPos += inCount;
                stretch.process (in, inCount, outBuf, chunk);
                for (int i = 0; i < chunk; ++i)
                {
                    const int dest = written + i;
                    if (dest < 0 || dest >= frames) continue;
                    for (int c = 0; c < 2; ++c) out->buffer.setSample (c, dest, outBuf[(size_t) c][(size_t) i]);
                }
                written += chunk;
            }
            break;
        }
    }
    return out;
}

std::shared_ptr<const CachedAudio> AudioCache::forClip (const Clip& clip, const TempoMap& tempo, const File& folder)
{
    const auto& a = clip.audio;
    File f = File::isAbsolutePath (a.renderedFile.empty() ? a.file : a.renderedFile)
                 ? File (a.renderedFile.empty() ? a.file : a.renderedFile)
                 : folder.getChildFile (a.renderedFile.empty() ? a.file : a.renderedFile);
    const bool warped = a.warp != WarpMode::Off || a.reversed || std::abs (a.transpose) > 1e-6;

    String key = "file:" + f.getFullPathName() + "@" + String (rate);
    if (warped)
    {
        // Everything the render depends on.
        String k;
        k << "warp:" << f.getFullPathName() << "@" << rate << ":" << (int) a.warp << ":" << a.sourceStart << ":"
          << a.sourceBpm << ":" << a.transpose << ":" << (int) a.reversed << ":" << clip.start << ":" << clip.offset
          << ":" << clip.length << ":" << clip.loopLength;
        for (auto& m : a.markers) k << ":" << m.sourceSeconds << "/" << m.tick;
        for (auto& tp : tempo.tempos()) k << "|" << tp.tick << "/" << tp.bpm << "/" << (int) tp.rampToNext;
        key = k;
    }
    {
        const ScopedLock sl (lock);
        if (auto it = ready.find (key); it != ready.end()) return it->second;
        if (working[key]) return nullptr;
        working[key] = true;
    }
    const auto r = rate;
    pool.addJob ([this, key, f, clip, tempo, warped, r]
    {
        auto plain = file (f);
        std::shared_ptr<const CachedAudio> result = plain;
        if (plain && warped) result = render (*plain, clip, tempo, r);
        {
            const ScopedLock sl (lock);
            if (result) ready[key] = result;
            working.erase (key);
        }
        triggerAsyncUpdate();
    });
    return nullptr;
}

void AudioCache::handleAsyncUpdate()
{
    if (onReady) onReady();
}

} // namespace kal
