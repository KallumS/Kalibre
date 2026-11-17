#include "TrackProcessors.h"

#include "Edit.h"
#include "Surround.h"

namespace kal
{
using namespace juce;

//==============================================================================
// Eight channels, for up to 7.1 (decision 0040); a stereo mix uses the first two.
Fader::Fader()
    : AudioProcessor (BusesProperties().withInput ("In", AudioChannelSet::discreteChannels (8)).withOutput ("Out", AudioChannelSet::discreteChannels (8)))
{
}

void Fader::processBlock (AudioBuffer<float>& b, MidiBuffer& m)
{
    // What comes out goes to the analysers too, once the block is done.
    struct Tap
    {
        Fader& f; AudioBuffer<float>& b;
        ~Tap()
        {
            if (! f.tap.load() || b.getNumChannels() < 2) return;
            int w = f.tapWrite.load();
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                f.tapL[(size_t) w].store (b.getSample (0, i), std::memory_order_relaxed);
                f.tapR[(size_t) w].store (b.getSample (1, i), std::memory_order_relaxed);
                w = (w + 1) % tapSize;
            }
            f.tapWrite = w;
        }
    } tapOnExit { *this, b };
    m.clear();
    const int n = b.getNumSamples();
    if (b.getNumChannels() < 2) return;
    const float autoVol = automatedVolume.load();
    const float autoPan = automatedPan.load();
    const float db = std::isnan (autoVol) ? volumeDb.load() : autoVol;
    const float p = jlimit (-1.0f, 1.0f, std::isnan (autoPan) ? pan.load() : autoPan);
    float gain = audible && automatedMute.load() != 1 ? Decibels::decibelsToGain (db, -100.0f) : 0.0f;
    if (phaseInvert) gain = -gain;
    lastGain = gain;
    const auto fmt = (SurroundFormat) format.load();
    if (fmt != SurroundFormat::Stereo && b.getNumChannels() >= surround::channels (fmt))
    {
        // Surround: a bus passes every channel at its level; a track is placed among the speakers.
        const int chans = surround::channels (fmt);
        smoothedGain.setTargetValue (gain);
        const auto spk = surround::gains (fmt, p, frontBack.load());
        for (int ch = 0; ch < 8; ++ch) speaker[(size_t) ch].setTargetValue (spk[(size_t) ch]);
        float pk[2] {};
        for (int i = 0; i < n; ++i)
        {
            const float g = smoothedGain.getNextValue();
            if (bus.load())
                for (int ch = 0; ch < chans; ++ch) b.getWritePointer (ch)[i] *= g;
            else
            {
                const float m = (b.getSample (0, i) + b.getSample (1, i)) * 0.5f * g;
                for (int ch = 0; ch < chans; ++ch) b.getWritePointer (ch)[i] = m * speaker[(size_t) ch].getNextValue();
            }
            pk[0] = jmax (pk[0], std::abs (b.getSample (0, i)));
            pk[1] = jmax (pk[1], std::abs (b.getSample (1, i)));
        }
        for (int ch = chans; ch < b.getNumChannels(); ++ch) b.clear (ch, 0, n);
        float old = peakL.load();
        while (pk[0] > old && ! peakL.compare_exchange_weak (old, pk[0])) {}
        old = peakR.load();
        while (pk[1] > old && ! peakR.compare_exchange_weak (old, pk[1])) {}
        level = jmax (pk[0], pk[1]);
        if (measureLoudness.load())
        {
            if (resetLoudness.exchange (false)) loudnessMeter.reset();
            loudnessMeter.process (b.getWritePointer (0), b.getWritePointer (1), n);
        }
        return;
    }
    for (int ch = 2; ch < b.getNumChannels(); ++ch) b.clear (ch, 0, n);
    // Constant-power pan law: -3 dB in the middle.
    const float angle = (p + 1.0f) * MathConstants<float>::pi * 0.25f;
    smoothedGain.setTargetValue (gain);
    smoothedL.setTargetValue (std::cos (angle) * MathConstants<float>::sqrt2);
    smoothedR.setTargetValue (std::sin (angle) * MathConstants<float>::sqrt2);

    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    float pl = 0, pr = 0;
    for (int i = 0; i < n; ++i)
    {
        float a = l[i], c = r[i];
        if (mono) a = c = (a + c) * 0.5f;
        const float g = smoothedGain.getNextValue();
        l[i] = a * g * smoothedL.getNextValue();
        r[i] = c * g * smoothedR.getNextValue();
        pl = jmax (pl, std::abs (l[i]));
        pr = jmax (pr, std::abs (r[i]));
    }
    // The meters keep the highest since they were last read.
    float old = peakL.load();
    while (pl > old && ! peakL.compare_exchange_weak (old, pl)) {}
    old = peakR.load();
    while (pr > old && ! peakR.compare_exchange_weak (old, pr)) {}
    level = jmax (pl, pr);
    // RMS over about 300 ms: a block's mean square into a running average.
    if (n > 0)
    {
        double sl = 0, sr = 0;
        for (int i = 0; i < n; ++i) { sl += (double) l[i] * l[i]; sr += (double) r[i] * r[i]; }
        const double k = 1.0 - std::exp (-(double) n / (getSampleRate() > 0 ? getSampleRate() * 0.3 : 14400.0));
        meanL += (sl / n - meanL) * k;
        meanR += (sr / n - meanR) * k;
        rmsL = (float) std::sqrt (meanL);
        rmsR = (float) std::sqrt (meanR);
    }
    if (measureLoudness.load())
    {
        if (resetLoudness.exchange (false)) loudnessMeter.reset();
        loudnessMeter.process (l, r, n);
    }
}

//==============================================================================
TrackSource::TrackSource (Shared& s, Id track)
    : AudioProcessor (BusesProperties().withInput ("In", AudioChannelSet::stereo()).withOutput ("Out", AudioChannelSet::stereo())),
      trackId (track), shared (s)
{
    writerThread.startThread();
}

TrackSource::~TrackSource()
{
    writing = false;
    writer.reset();
    writerThread.stopThread (2000);
}

void TrackSource::prepareToPlay (double rate, int block)
{
    live.setSize (2, block);
    for (auto& r : runners) r.prepare (rate);
    if (keeping) history.setSize (2, (int) (rate * 60));
}

void TrackSource::keepInput (bool keep)
{
    if (keep && history.getNumSamples() == 0)
    {
        history.setSize (2, (int) (jmax (8000.0, getSampleRate()) * 60));
        history.clear();
        historyWrite = 0;
    }
    keeping = keep;
}

AudioBuffer<float> TrackSource::recentInput (double seconds) const
{
    const int size = history.getNumSamples();
    const int n = jmin (size, (int) (seconds * getSampleRate()));
    AudioBuffer<float> out (2, jmax (0, n));
    const int end = historyWrite.load();
    for (int i = 0; i < n; ++i)
    {
        const int p = (end - n + i + size) % size;
        out.setSample (0, i, history.getSample (0, p));
        out.setSample (1, i, history.getSample (1, p));
    }
    return out;
}

void TrackSource::startRecording (const File& f, double rate)
{
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    WavAudioFormat wav;
    auto stream = f.createOutputStream();
    if (! stream) return;
    std::unique_ptr<OutputStream> os (stream.release());
    auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
    if (! w) return;
    recordFile = f;
    recordStart = -1;
    writer = std::make_unique<AudioFormatWriter::ThreadedWriter> (w.release(), writerThread, 1 << 17);
    writing = true;
}

File TrackSource::stopRecording()
{
    writing = false;
    // The audio thread may be mid-write: give it a block to see the flag.
    Thread::sleep (30);
    writer.reset();
    return recordFile;
}

void TrackSource::caught (const MidiBuffer& midi, bool fromOutput)
{
    const auto& clock = shared.clock;
    if (! recordOutput.load (std::memory_order_relaxed) || ! clock.recording || ! clock.playing) return;
    for (const auto m : midi)
    {
        const auto& msg = m.getMessage();
        if (msg.getRawDataSize() > 3 || msg.isSysEx()) continue;
        int s1, n1, s2, n2;
        tappedFifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 + n2 == 0) return;
        auto& slot = tapped[(size_t) (n1 > 0 ? s1 : s2)];
        slot.seconds = clock.secondsAt (m.samplePosition);
        for (int i = 0; i < 3; ++i) slot.bytes[i] = i < msg.getRawDataSize() ? msg.getRawData()[i] : 0;
        slot.fromOutput = fromOutput;
        tappedFifo.finishedWrite (1);
    }
}

void TrackSource::stopAll (MidiBuffer& midi, int at)
{
    waitingCount = 0;      // notes played late that hadn't started yet: not now
    skipped.fill (0);
    for (int ch = 0; ch < 16; ++ch)
    {
        bool any = false;
        for (int note = 0; note < 128; ++note)
        {
            auto& count = sounding[(size_t) (ch * 128 + note)];
            if (count > 0)
            {
                midi.addEvent (MidiMessage::noteOff (ch + 1, note), at);
                count = 0;
                any = true;
            }
        }
        if (any) midi.addEvent (MidiMessage::controllerEvent (ch + 1, 64, 0), at);
    }
}

// Sends one message, keeping count of the notes sounding so each is ended once.
void TrackSource::emit (const Data& d, const std::uint8_t* bytes, int size, int at, MidiBuffer& midi)
{
    const int status = bytes[0] & 0xF0, ch = bytes[0] & 15;
    if (status == 0x90 && bytes[2] > 0)
    {
        if (! d.audible) return;
        ++sounding[(size_t) (ch * 128 + bytes[1])];
    }
    else if (status == 0x80 || status == 0x90)
    {
        auto& c = sounding[(size_t) (ch * 128 + bytes[1])];
        if (c == 0) return;
        --c;
    }
    midi.addEvent (bytes, size, at);
}

void TrackSource::playSegment (const Data& d, double from, double to, int offset, int length,
                               AudioBuffer<float>& audio, MidiBuffer& midi)
{
    const double rate = shared.clock.rate > 0 ? shared.clock.rate : getSampleRate();
    // MIDI from the compiled list.
    const auto& list = d.compiled.midi;
    if (std::abs (from - lastEnd) > 1e-6)
    {
        nextMidi = playback::firstAtOrAfter (list, from);
        waitingCount = 0;       // a jump: what was waiting belonged to where it was
        skipped.fill (0);
    }
    auto sampleOf = [&] (double s) { return offset + jlimit (0, length - 1, (int) ((s - from) * rate)); };
    // Notes played late, whose moment has come.
    auto flushWaiting = [&] (double until)
    {
        for (int i = 0; i < waitingCount;)
        {
            if (waiting[(size_t) i].seconds < until)
            {
                const auto w = waiting[(size_t) i];
                waiting[(size_t) i] = waiting[(size_t) --waitingCount];
                emit (d, w.bytes, 3, sampleOf (std::max (from, w.seconds)), midi);
            }
            else ++i;
        }
    };
    while (nextMidi < list.size() && list[nextMidi].seconds < to)
    {
        const auto& m = list[nextMidi++];
        if (m.seconds < from) continue;
        const int status = m.bytes[0] & 0xF0, key = (m.bytes[0] & 15) * 128 + (m.bytes[1] & 127);
        if (status == 0x90 && m.bytes[2] > 0 && (m.chance < 100 || m.velocityRange > 0 || m.jitter > 0))
        {
            // Played differently each time (decision 0061).
            const auto r = playback::roll (m, dice);
            if (! r.plays) { ++skipped[(size_t) key]; continue; }
            std::uint8_t bytes[3] { m.bytes[0], m.bytes[1], r.velocity };
            if (r.delay > 0 && waitingCount < (int) waiting.size())
            {
                waiting[(size_t) waitingCount++] = { m.seconds + r.delay, { bytes[0], bytes[1], bytes[2] } };
                continue;
            }
            emit (d, bytes, 3, sampleOf (m.seconds), midi);
            continue;
        }
        if ((status == 0x80 || (status == 0x90 && m.bytes[2] == 0)) && skipped[(size_t) key] > 0)
        {
            --skipped[(size_t) key];   // the end of a note that didn't play this time
            continue;
        }
        // A note's end waits for its start, if that's still waiting.
        flushWaiting (m.seconds + 1e-9);
        emit (d, m.bytes, m.size, sampleOf (m.seconds), midi);
    }
    flushWaiting (to);
    lastEnd = to;

    // Audio regions overlapping [from, to).
    if (! d.audible) return;
    for (size_t i = 0; i < d.compiled.audio.size(); ++i)
    {
        const auto& r = d.compiled.audio[i];
        if (r.endSeconds <= from || r.startSeconds >= to) continue;
        const auto& src = d.regionAudio[i];
        if (! src) continue;
        const auto& buf = src->buffer;
        const double regionLength = r.endSeconds - r.startSeconds;
        // In whole samples, so a one-sample click is never skipped or doubled.
        const double first = from * rate - r.startSeconds * rate + r.sourceSeconds * rate;
        const int lastChannel = jmin (1, buf.getNumChannels() - 1);
        for (int s = 0; s < length; ++s)
        {
            const double t = from + s / rate;
            if (t < r.startSeconds - 0.5 / rate || t >= r.endSeconds) continue;
            const auto p = (int) std::llround (first + s);
            if (p < 0 || p >= buf.getNumSamples()) continue;
            const double into = t - r.startSeconds;
            float g = (float) r.gain;
            g *= (float) playback::fadeGain (into, r.fadeIn);
            g *= (float) playback::fadeGain (regionLength - into, r.fadeOut);
            audio.addSample (0, offset + s, g * buf.getSample (0, p));
            audio.addSample (1, offset + s, g * buf.getSample (lastChannel, p));
        }
    }
}

void TrackSource::applyAutomation (const Data& d, double seconds)
{
    const Tick tick = d.tempo.tickAt (seconds);
    if (d.readAutomation)
    {
        for (auto& pl : d.params)
        {
            if (! pl.param) continue;
            const auto v = (float) edit::valueAt (pl.lane, tick, pl.param->getValue());
            if (std::abs (v - pl.param->getValue()) > 1.0e-5f) pl.param->setValue (v);
        }
        if (d.fader)
        {
            // Volume lanes run 0-1 over -60..+6 dB; pan over -1..1.
            d.fader->automatedVolume = d.hasVolume ? (float) (edit::valueAt (d.volume, tick, 0.9) * 66.0 - 60.0)
                                                   : std::numeric_limits<float>::quiet_NaN();
            d.fader->automatedPan = d.hasPan ? (float) (edit::valueAt (d.pan, tick, 0.5) * 2.0 - 1.0)
                                             : std::numeric_limits<float>::quiet_NaN();
            d.fader->automatedMute = d.hasMute ? (edit::valueAt (d.mute, tick, 0.0) >= 0.5 ? 1 : 0) : -1;
        }
        // Send levels run over -60..+6 dB like the fader.
        for (auto& [lane, send] : d.sends)
            if (send) send->automatedDb = (float) (edit::valueAt (lane, tick, 0.9) * 66.0 - 60.0);
    }
    else if (d.fader)
    {
        // Automation off: the controls are as they're set.
        d.fader->automatedVolume = std::numeric_limits<float>::quiet_NaN();
        d.fader->automatedPan = std::numeric_limits<float>::quiet_NaN();
        d.fader->automatedMute = -1;
        for (auto& [lane, send] : d.sends) if (send) send->automatedDb = std::numeric_limits<float>::quiet_NaN();
    }
}

void TrackSource::processBlock (AudioBuffer<float>& audio, MidiBuffer& midi)
{
    const int n = audio.getNumSamples();
    const Data* d = data.take();
    const auto& clock = shared.clock;

    // What comes in live: kept when monitoring, recorded when armed.
    if (n <= live.getNumSamples())
        for (int c = 0; c < 2; ++c) live.copyFrom (c, 0, audio, jmin (c, audio.getNumChannels() - 1), 0, n);

    if (keeping && history.getNumSamples() > 0)
    {
        const int size = history.getNumSamples();
        int w = historyWrite.load();
        for (int i = 0; i < n; ++i)
        {
            history.setSample (0, w, live.getSample (0, i));
            history.setSample (1, w, live.getSample (1, i));
            w = (w + 1) % size;
        }
        historyWrite = w;
    }

    if (writing && writer && clock.recording && clock.playing)
    {
        if (recordStart < 0) recordStart = clock.start - recordLatencySeconds.load();
        const float* chans[2] { live.getReadPointer (0), live.getReadPointer (1) };
        writer->write (chans, n);
    }

    // Live MIDI: kept (it only arrives when this track is the one played),
    // and remembered with its place in the song when recording.
    if (d && d->armedMidi && clock.recording && clock.playing)
        for (const auto m : midi)
        {
            const auto& msg = m.getMessage();
            if (msg.getRawDataSize() > 3) continue;
            int s1, n1, s2, n2;
            playedFifo.prepareToWrite (1, s1, n1, s2, n2);
            if (n1 + n2 == 0) break;
            auto& slot = played[(size_t) (n1 > 0 ? s1 : s2)];
            slot.ticks = d->tempo.ticksAt (clock.secondsAt (m.samplePosition));
            for (int i = 0; i < 3; ++i) slot.bytes[i] = i < msg.getRawDataSize() ? msg.getRawData()[i] : 0;
            playedFifo.finishedWrite (1);
        }

    if (! d || ! d->monitor) audio.clear();

    if (! d)
    {
        midi.clear();
        return;
    }

    if (clock.jumped || (! clock.playing && lastEnd >= 0))
    {
        stopAll (midi, 0);
        lastEnd = -1;
    }

    // A Summing track recording the master doesn't play into it too (decision 0052).
    if (clock.playing && ! (d->summing && d->armed && clock.recording))
    {
        applyAutomation (*d, clock.start);
        if (clock.wrapAt >= 0)
        {
            playSegment (*d, clock.start, clock.start + clock.wrapAt / clock.rate, 0, clock.wrapAt, audio, midi);
            stopAll (midi, clock.wrapAt);
            lastEnd = -1;
            playSegment (*d, clock.wrapTo, clock.wrapTo + (n - clock.wrapAt) / clock.rate, clock.wrapAt, n - clock.wrapAt, audio, midi);
        }
        else
            playSegment (*d, clock.start, clock.start + n / clock.rate, 0, n, audio, midi);
    }

    // Modulators: one value a block, pushed onto their targets.
    modulation::Clock mc { clock.rate, n, clock.playing, clock.ppq, clock.bpm };
    for (size_t i = 0; i < d->modulators.size() && i < runners.size(); ++i)
    {
        auto& run = d->modulators[i];
        if (runnerIds[i] != run.mod.id) { runners[i].reset(); runnerIds[i] = run.mod.id; }
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            if (msg.isNoteOn()) runners[i].noteOn();
            else if (msg.isNoteOff()) runners[i].noteOff();
        }
        const double level = run.followLevel ? run.followLevel->load() : (d->fader ? d->fader->level.load() : 0.0f);
        const double v = runners[i].advance (run.mod, mc, level);
        for (size_t t = 0; t < run.params.size() && t < run.mod.targets.size(); ++t)
            if (auto* p = run.params[t])
                p->setValue ((float) modulation::applied (run.bases[t], v, run.mod.targets[t].amount));
    }
}

//==============================================================================
void Metronome::processBlock (AudioBuffer<float>& audio, MidiBuffer& midi)
{
    midi.clear();
    audio.clear();
    const auto* s = settings.take();
    const auto& clock = shared.clock;
    if (! s) return;
    // A count-in clicks whatever the settings say, until the song starts.
    const bool counting = clock.playing && clock.start < countInUntil.load();
    if (! counting && (! s->m.on || ! clock.playing || (s->m.onlyWhileRecording && ! clock.recording))) return;

    const int n = audio.getNumSamples();
    const double rate = clock.rate;
    const float gain = Decibels::decibelsToGain ((float) s->m.volumeDb);
    for (int i = 0; i < n; ++i)
    {
        // A click starts on every beat (or subdivision) crossed in this sample.
        {
            const double t0 = clock.secondsAt (i), t1 = clock.secondsAt (i + 1);
            const double q0 = s->tempo.ticksAt (t0), q1 = s->tempo.ticksAt (t1);
            const auto& meter = s->tempo.meterAt ((Tick) q0);
            const Tick step = meter.beatTicks() / jmax (1, s->m.subdivision);
            const Tick barStart = s->tempo.barStart (s->tempo.barAt ((Tick) q1));
            const Tick nextClick = (Tick) std::ceil ((q0 - (double) barStart) / (double) step) * step + barStart;
            if ((double) nextClick >= q0 && (double) nextClick < q1)
            {
                clickLeft = (int) (rate * 0.03);
                phase = 0;
                accent = s->m.accentFirst && nextClick == barStart;
            }
        }
        if (clickLeft > 0)
        {
            const double f = accent ? 1760.0 : 1320.0;
            const auto env = (float) clickLeft / (float) (rate * 0.03);
            const auto v = gain * env * env * (float) std::sin (phase);
            phase += MathConstants<double>::twoPi * f / rate;
            audio.setSample (0, i, v);
            audio.setSample (1, i, v);
            --clickLeft;
        }
    }
}

} // namespace kal
