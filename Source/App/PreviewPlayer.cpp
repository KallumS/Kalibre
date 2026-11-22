#include "PreviewPlayer.h"

#include "BuiltinInstruments.h"
#include "Engine.h"
#include "Warp.h"

namespace kal
{
using namespace juce;

PreviewPlayer::PreviewPlayer (Engine& e) : engine (e)
{
    engine.devices().addAudioCallback (this);
}

PreviewPlayer::~PreviewPlayer()
{
    *alive = false;
    engine.devices().removeAudioCallback (this);
    pool.removeAllJobs (true, 4000);
}

double PreviewPlayer::progress() const
{
    const int n = length.load();
    return n > 0 && position.load() < n ? jlimit (0.0, 1.0, (double) position.load() / n) : -1.0;
}

double PreviewPlayer::secondsToNextBeat (const Engine& e, const TempoMap& tempo)
{
    if (! e.isPlaying()) return 0;
    const double now = e.position();
    const Tick at = tempo.tickAt (now);
    const Tick next = ((at + PPQ - 1) / PPQ) * PPQ;
    return jmax (0.0, tempo.secondsAt (next) - now);
}

std::unique_ptr<AudioBuffer<float>> PreviewPlayer::render (const library::Item& item, bool sync, double songBpm, double rate)
{
    auto out = std::make_unique<AudioBuffer<float>>();
    const File file (item.path);
    if (item.kind == library::Kind::Midi)
    {
        FileInputStream in (file);
        MidiFile mf;
        if (! in.openedOk() || ! mf.readFrom (in)) return nullptr;
        const double tpq = mf.getTimeFormat() > 0 ? mf.getTimeFormat() : 960.0;
        // At the song's tempo with Sync on; otherwise at the file's own (or 120).
        const double bpm = sync && songBpm > 0 ? songBpm : item.bpm > 0 ? item.bpm : 120.0;
        const double perTick = 60.0 / bpm / tpq * rate;   // frames
        MidiMessageSequence all;
        for (int t = 0; t < mf.getNumTracks(); ++t) all.addSequence (*mf.getTrack (t), 0);
        all.sort();
        double lastFrame = 0;
        for (auto* ev : all) if (ev->message.isNoteOnOrOff()) lastFrame = jmax (lastFrame, ev->message.getTimeStamp() * perTick);
        const int frames = (int) jmin (longest * rate, lastFrame + rate);   // a second to ring out
        if (frames <= 0) return nullptr;
        KalibreSynth synth;
        const int block = 512;
        synth.setPlayConfigDetails (0, 2, rate, block);
        synth.prepareToPlay (rate, block);
        out->setSize (2, frames);
        out->clear();
        AudioBuffer<float> chunk (2, block);
        int next = 0;
        for (int done = 0; done < frames; done += block)
        {
            const int n = jmin (block, frames - done);
            MidiBuffer midi;
            for (; next < all.getNumEvents(); ++next)
            {
                const auto& m = all.getEventPointer (next)->message;
                const int frame = (int) std::llround (m.getTimeStamp() * perTick);
                if (frame >= done + n) break;
                if (m.isNoteOnOrOff() || m.isController() || m.isPitchWheel())
                    midi.addEvent (m, jlimit (0, n - 1, frame - done));
            }
            if (done + n >= frames) midi.addEvent (MidiMessage::allNotesOff (1), 0);
            AudioBuffer<float> view (chunk.getArrayOfWritePointers(), 2, n);
            view.clear();
            synth.processBlock (view, midi);
            for (int c = 0; c < 2; ++c) out->copyFrom (c, done, view, c, 0, n);
        }
        return out;
    }

    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (file));
    if (! r || r->sampleRate <= 0) return nullptr;
    const int fileFrames = (int) jmin<int64> (r->lengthInSamples, (int64) (longest * r->sampleRate));
    if (fileFrames <= 0) return nullptr;
    AudioBuffer<float> raw (2, fileFrames);
    r->read (&raw, 0, fileFrames, 0, true, r->numChannels > 1);
    if (r->numChannels == 1) raw.copyFrom (1, 0, raw, 0, 0, fileFrames);

    // To the device's rate.
    CachedAudio source;
    source.sampleRate = rate;
    const double ratio = r->sampleRate / rate;
    const int frames = jmax (1, (int) std::floor (fileFrames / ratio));
    source.buffer.setSize (2, frames);
    for (int c = 0; c < 2; ++c)
    {
        if (std::abs (ratio - 1.0) < 1e-9) { source.buffer.copyFrom (c, 0, raw, c, 0, frames); continue; }
        LagrangeInterpolator li;
        li.process (ratio, raw.getReadPointer (c), source.buffer.getWritePointer (c), frames, fileFrames, 0);
    }

    // Stretched to the song's tempo, as a warped clip is, when Sync is on and its tempo is known.
    const double seconds = frames / rate;
    if (sync && item.tempoTrusted() && songBpm > 0 && std::abs (songBpm / item.bpm - 1.0) > 0.002)
    {
        TempoMap tempo;
        tempo.setTempoAt (0, songBpm);
        Clip clip;
        clip.kind = Clip::Kind::Audio;
        clip.start = 0;
        clip.length = jmax<Tick> (1, (Tick) std::llround (seconds * item.bpm / 60.0 * PPQ));
        clip.audio.file = item.path;
        clip.audio.sourceLength = seconds;
        clip.audio.fileSampleRate = rate;
        clip.audio.sourceBpm = item.bpm;
        clip.audio.warp = WarpMode::Spectral;
        if (auto stretched = AudioCache::render (source, clip, tempo, rate))
        {
            *out = std::move (stretched->buffer);
            return out;
        }
    }
    *out = std::move (source.buffer);
    return out;
}

void PreviewPlayer::play (const library::Item& item, bool sync, double songBpm)
{
    const int id = ++requested;
    path = String (item.path);
    const double rate = deviceRate.load();
    auto alivePtr = alive;
    pool.removeAllJobs (false, 0);
    pool.addJob ([this, item, sync, songBpm, rate, id, alivePtr]
    {
        auto buffer = render (item, sync, songBpm, rate);
        MessageManager::callAsync ([this, buffer = std::shared_ptr<AudioBuffer<float>> (std::move (buffer)), id, sync, rate, alivePtr]
        {
            if (! *alivePtr || id != requested.load()) return;   // gone, or another was asked for since
            auto s = std::make_unique<Sound>();
            s->id = id;
            if (buffer) s->buffer = std::move (*buffer);
            s->delay = sync && nextBeat ? (int) std::llround (nextBeat() * rate) : 0;
            length = s->buffer.getNumSamples();
            position = 0;
            swap.post (std::move (s));
        });
    });
}

void PreviewPlayer::remake (const library::Item& item, bool sync, double songBpm)
{
    const int id = ++requested;
    const double rate = deviceRate.load();
    auto alivePtr = alive;
    pool.removeAllJobs (false, 0);
    pool.addJob ([this, item, sync, songBpm, rate, id, alivePtr]
    {
        auto buffer = render (item, sync, songBpm, rate);
        MessageManager::callAsync ([this, buffer = std::shared_ptr<AudioBuffer<float>> (std::move (buffer)), id, alivePtr]
        {
            if (! *alivePtr || id != requested.load()) return;
            // How far through it is now, while the new one was being made.
            const double through = progress();
            if (through < 0) return;   // it ended meanwhile: nothing to carry on
            auto s = std::make_unique<Sound>();
            s->id = id;
            if (buffer) s->buffer = std::move (*buffer);
            s->startAt = (int) std::llround (through * s->buffer.getNumSamples());
            length = s->buffer.getNumSamples();
            position = s->startAt;
            swap.post (std::move (s));
        });
    });
}

void PreviewPlayer::stop()
{
    const int id = ++requested;
    path = {};
    auto s = std::make_unique<Sound>();
    s->id = id;
    length = 0;
    position = 0;
    swap.post (std::move (s));
}

void PreviewPlayer::audioDeviceAboutToStart (AudioIODevice* d)
{
    if (d) deviceRate = d->getCurrentSampleRate();
}

void PreviewPlayer::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outs, int numOuts, int n,
                                                      const AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOuts; ++c) if (outs[c]) FloatVectorOperations::clear (outs[c], n);
    auto* s = swap.take();
    if (! s) return;
    if (s->id != playingId) { playingId = s->id; pos = s->startAt >= 0 ? s->startAt : -s->delay; }
    const int total = s->buffer.getNumSamples();
    if (pos >= total) return;
    const float gain = 0.8f;
    for (int i = 0; i < n; ++i, ++pos)
    {
        if (pos < 0 || pos >= total) continue;
        for (int c = 0; c < numOuts && c < 2; ++c)
            if (outs[c]) outs[c][i] = s->buffer.getSample (c, pos) * gain;
    }
    position = jmax (0, pos);
}

} // namespace kal
