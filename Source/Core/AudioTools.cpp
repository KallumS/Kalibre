#include "AudioTools.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace kal::audiotools
{

static constexpr double pi = 3.141592653589793;

int nearestZeroCrossing (const float* const* channels, int numChannels, int numSamples, int at, int reach)
{
    if (numSamples <= 0 || numChannels <= 0) return at;
    auto mixed = [&] (int i)
    {
        float v = 0;
        for (int ch = 0; ch < numChannels; ++ch) v += channels[ch][i];
        return v;
    };
    // A crossing between i - 1 and i, or i itself zero: the nearer to zero of the two.
    auto crossingAt = [&] (int i) -> int
    {
        if (i < 0 || i >= numSamples) return -1;
        const float b = mixed (i);
        if (b == 0.0f) return i;
        if (i == 0) return -1;
        const float a = mixed (i - 1);
        if ((a < 0.0f) == (b < 0.0f)) return -1;
        return std::abs (a) < std::abs (b) ? i - 1 : i;
    };
    for (int d = 0; d <= reach; ++d)
    {
        if (const int z = crossingAt (at - d); z >= 0) return z;
        if (d > 0) if (const int z = crossingAt (at + d); z >= 0) return z;
    }
    return at;
}

float peak (const float* const* ch, int n, int start, int end)
{
    float p = 0;
    for (int c = 0; c < n; ++c)
        for (int i = start; i < end; ++i) p = std::max (p, std::abs (ch[c][i]));
    return p;
}

double normaliseGainDb (float peakValue, double targetDb)
{
    if (peakValue <= 0) return 0;
    return targetDb - 20.0 * std::log10 ((double) peakValue);
}

//==============================================================================
std::vector<Span> sounding (const float* const* ch, int numChannels, int numSamples, double rate,
                            double thresholdDb, double minSilence, double pad)
{
    const int window = std::max (1, (int) (rate * 0.01));
    const float threshold = (float) std::pow (10.0, thresholdDb / 20.0);
    std::vector<Span> loud;
    for (int w = 0; w < numSamples; w += window)
    {
        const int e = std::min (numSamples, w + window);
        if (peak (ch, numChannels, w, e) < threshold) continue;
        if (! loud.empty() && loud.back().end == w) loud.back().end = e;
        else loud.push_back ({ w, e });
    }
    // Short silences are part of the sound.
    const int gap = (int) (minSilence * rate);
    std::vector<Span> merged;
    for (auto s : loud)
    {
        if (! merged.empty() && s.start - merged.back().end < gap) merged.back().end = s.end;
        else merged.push_back (s);
    }
    const int p = (int) (pad * rate);
    for (auto& s : merged) { s.start = std::max (0, s.start - p); s.end = std::min (numSamples, s.end + p); }
    // Padding can make neighbours touch.
    std::vector<Span> out;
    for (auto s : merged)
    {
        if (! out.empty() && s.start <= out.back().end) out.back().end = std::max (out.back().end, s.end);
        else out.push_back (s);
    }
    return out;
}

int hitGapFrames (double rate)
{
    const int hop = std::max (16, (int) (rate * 0.005));
    return (int) (0.05 * rate / hop);
}

bool isHit (const HitCandidate& h, double sensitivity, int lastFrame, int gapFrames)
{
    sensitivity = std::clamp (sensitivity, 0.0, 1.0);
    const double rise = 12.0 - 8.0 * sensitivity;          // dB above the frames just before
    const double floorDb = -50.0 - 20.0 * sensitivity;     // and loud enough to be a hit
    return h.levelDb >= floorDb && h.rise >= rise && h.frame - lastFrame >= gapFrames;
}

std::vector<HitCandidate> hitCandidates (const float* const* ch, int numChannels, int numSamples, double rate)
{
    const int hop = std::max (16, (int) (rate * 0.005));
    const int frames = numSamples / hop;
    std::vector<double> energy ((size_t) std::max (0, frames));
    for (int f = 0; f < frames; ++f)
    {
        double e = 0;
        for (int c = 0; c < numChannels; ++c)
            for (int i = f * hop; i < (f + 1) * hop; ++i) e += (double) ch[c][i] * ch[c][i];
        energy[(size_t) f] = 10.0 * std::log10 (e / (hop * std::max (1, numChannels)) + 1e-12);
    }
    std::vector<HitCandidate> out;
    for (int f = 1; f < frames; ++f)
    {
        double before = -120;
        for (int k = std::max (0, f - 4); k < f; ++k) before = std::max (before, energy[(size_t) k]);
        const double now = energy[(size_t) f];
        if (now < -70.0 || now - before < 4.0) continue;   // not a hit at any sensitivity
        // The frame's first sample louder than a tenth of its loudest.
        float top = 0;
        for (int c = 0; c < numChannels; ++c)
            for (int i = f * hop; i < (f + 1) * hop; ++i) top = std::max (top, std::abs (ch[c][i]));
        int at = f * hop;
        for (int i = (f - 1) * hop; i < (f + 1) * hop; ++i)
        {
            float v = 0;
            for (int c = 0; c < numChannels; ++c) v = std::max (v, std::abs (ch[c][i]));
            if (v >= top * 0.1f) { at = i; break; }
        }
        out.push_back ({ at, f, now - before, now });
    }
    return out;
}

std::vector<int> transients (const float* const* ch, int numChannels, int numSamples, double rate, double sensitivity)
{
    const int gap = hitGapFrames (rate);
    std::vector<int> out;
    int last = -gap - 1;
    for (const auto& h : hitCandidates (ch, numChannels, numSamples, rate))
        if (isHit (h, sensitivity, last, gap)) { out.push_back (h.at); last = h.frame; }
    return out;
}

//==============================================================================
double sourceSecondsAt (const AudioData& a, Tick t)
{
    auto markers = a.markers;
    std::sort (markers.begin(), markers.end(), [] (const WarpMarker& x, const WarpMarker& y) { return x.tick < y.tick; });
    if (markers.size() >= 2)
    {
        size_t i = 0;
        while (i + 2 < markers.size() && markers[i + 1].tick <= t) ++i;
        const auto& m0 = markers[i];
        const auto& m1 = markers[i + 1];
        if (m1.tick == m0.tick) return m0.sourceSeconds;
        const double f = (double) (t - m0.tick) / (double) (m1.tick - m0.tick);
        return m0.sourceSeconds + (m1.sourceSeconds - m0.sourceSeconds) * f;
    }
    const double offset = markers.size() == 1 ? markers[0].sourceSeconds - (a.sourceBpm > 0 ? (double) markers[0].tick / PPQ * 60.0 / a.sourceBpm : 0.0) : 0.0;
    if (a.sourceBpm > 0) return a.sourceStart + offset + (double) t / PPQ * 60.0 / a.sourceBpm;
    return -1;
}

Tick tickForSource (const AudioData& a, double s, const TempoMap& tempo, Tick contentStart)
{
    auto markers = a.markers;
    std::sort (markers.begin(), markers.end(), [] (const WarpMarker& x, const WarpMarker& y) { return x.tick < y.tick; });
    if (markers.size() >= 2)
    {
        size_t i = 0;
        while (i + 2 < markers.size() && markers[i + 1].sourceSeconds <= s) ++i;
        const auto& m0 = markers[i];
        const auto& m1 = markers[i + 1];
        if (m1.sourceSeconds == m0.sourceSeconds) return m0.tick;
        const double f = (s - m0.sourceSeconds) / (m1.sourceSeconds - m0.sourceSeconds);
        return m0.tick + (Tick) std::llround ((double) (m1.tick - m0.tick) * f);
    }
    if (a.sourceBpm > 0)
    {
        const double offset = markers.size() == 1 ? markers[0].sourceSeconds - (double) markers[0].tick / PPQ * 60.0 / a.sourceBpm : 0.0;
        return (Tick) std::llround ((s - a.sourceStart - offset) * a.sourceBpm / 60.0 * PPQ);
    }
    // At its own speed: seconds from the content's start, through the song's tempo.
    const double t0 = tempo.secondsAt (contentStart);
    return tempo.tickAt (t0 + (s - a.sourceStart)) - contentStart;
}

std::vector<WarpMarker> quantiseMarkers (const AudioData& a, const std::vector<double>& hits, const TempoMap& tempo,
                                         Tick contentStart, Tick contentLength, Tick grid, double strength)
{
    strength = std::clamp (strength, 0.0, 1.0);
    auto sourceAt = [&] (Tick t)
    {
        const double s = sourceSecondsAt (a, t);
        if (s >= 0) return s;
        return a.sourceStart + tempo.secondsAt (contentStart + t) - tempo.secondsAt (contentStart);
    };
    std::vector<WarpMarker> out;
    out.push_back ({ sourceAt (0), 0 });
    const WarpMarker last { sourceAt (contentLength), contentLength };
    auto sorted = hits;
    std::sort (sorted.begin(), sorted.end());
    const Tick minApart = std::max<Tick> (1, grid / 4);
    for (double s : sorted)
    {
        if (s <= out.back().sourceSeconds || s >= last.sourceSeconds) continue;
        const Tick t = tickForSource (a, s, tempo, contentStart);
        const Tick abs = contentStart + t;
        const Tick snapped = (Tick) std::llround ((double) abs / (double) grid) * grid - contentStart;
        const Tick moved = t + (Tick) std::llround ((double) (snapped - t) * strength);
        if (moved - out.back().tick < minApart || last.tick - moved < minApart) continue;
        out.push_back ({ s, moved });
    }
    out.push_back (last);
    return out;
}

//==============================================================================
void gain (float* const* ch, int n, int start, int end, float g)
{
    for (int c = 0; c < n; ++c)
        for (int i = start; i < end; ++i) ch[c][i] *= g;
}

void silence (float* const* ch, int n, int start, int end) { gain (ch, n, start, end, 0.0f); }

void reverse (float* const* ch, int n, int start, int end)
{
    for (int c = 0; c < n; ++c) std::reverse (ch[c] + start, ch[c] + end);
}

void fade (float* const* ch, int n, int start, int end, bool in)
{
    const int len = end - start;
    if (len <= 0) return;
    for (int i = 0; i < len; ++i)
    {
        // Equal power, like the clip fades (decision 0030).
        const double x = (double) i / len;
        const float g = (float) std::sin ((in ? x : 1.0 - x) * pi * 0.5);
        for (int c = 0; c < n; ++c) ch[c][start + i] *= g;
    }
}

void invert (float* const* ch, int n, int start, int end) { gain (ch, n, start, end, -1.0f); }

void removeDc (float* const* ch, int n, int start, int end)
{
    if (end <= start) return;
    for (int c = 0; c < n; ++c)
    {
        double sum = 0;
        for (int i = start; i < end; ++i) sum += ch[c][i];
        const float mean = (float) (sum / (end - start));
        for (int i = start; i < end; ++i) ch[c][i] -= mean;
    }
}

//==============================================================================
void fft (std::vector<std::complex<float>>& x, bool inverse)
{
    const size_t n = x.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (x[i], x[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double angle = 2 * pi / (double) len * (inverse ? 1 : -1);
        const std::complex<float> w ((float) std::cos (angle), (float) std::sin (angle));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> wn (1);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = x[i + k], v = x[i + k + len / 2] * wn;
                x[i + k] = u + v;
                x[i + k + len / 2] = u - v;
                wn *= w;
            }
        }
    }
    if (inverse)
        for (auto& v : x) v /= (float) n;
}

std::vector<float> spectrumDb (const float* s, int n)
{
    std::vector<std::complex<float>> x ((size_t) n);
    for (int i = 0; i < n; ++i)
        x[(size_t) i] = s[i] * (float) (0.5 - 0.5 * std::cos (2 * pi * i / (n - 1)));
    fft (x);
    std::vector<float> out ((size_t) n / 2);
    // A full-scale sine reads 0 dB (the Hann window halves the amplitude).
    const float scale = 4.0f / (float) n;
    for (size_t k = 0; k < out.size(); ++k)
        out[k] = 20.0f * std::log10 (std::abs (x[k]) * scale + 1e-9f);
    return out;
}

double pitchOf (const float* s, int n, double rate, double threshold)
{
    const int maxLag = std::min (n / 2, (int) (rate / 40.0));    // down to 40 Hz
    const int minLag = std::max (2, (int) (rate / 2000.0));     // up to 2 kHz
    if (maxLag <= minLag) return 0;
    double energy = 0;
    for (int i = 0; i < n; ++i) energy += (double) s[i] * s[i];
    if (energy / n < 1e-7) return 0;   // silence
    std::vector<double> d ((size_t) maxLag + 1, 0.0);
    const int w = n - maxLag;
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double sum = 0;
        for (int i = 0; i < w; ++i) { const double diff = s[i] - s[i + lag]; sum += diff * diff; }
        d[(size_t) lag] = sum;
    }
    // Cumulative mean normalised difference.
    double running = 0;
    std::vector<double> cm ((size_t) maxLag + 1, 1.0);
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        running += d[(size_t) lag];
        cm[(size_t) lag] = running > 0 ? d[(size_t) lag] * lag / running : 1.0;
    }
    int best = -1;
    for (int lag = minLag; lag <= maxLag; ++lag)
        if (cm[(size_t) lag] < threshold)
        {
            while (lag + 1 <= maxLag && cm[(size_t) lag + 1] < cm[(size_t) lag]) ++lag;
            best = lag;
            break;
        }
    if (best < 0) return 0;
    // Between samples, from the parabola through the three around the dip.
    double lag = best;
    if (best > 1 && best < maxLag)
    {
        const double a = cm[(size_t) best - 1], b = cm[(size_t) best], c = cm[(size_t) best + 1];
        const double den = a - 2 * b + c;
        if (std::abs (den) > 1e-12) lag += 0.5 * (a - c) / den;
    }
    return rate / lag;
}

void nearestNote (double hz, int& midiNote, double& cents, double a4)
{
    if (hz <= 0) { midiNote = -1; cents = 0; return; }
    const double exact = 69.0 + 12.0 * std::log2 (hz / a4);
    midiNote = (int) std::lround (exact);
    cents = (exact - midiNote) * 100.0;
}

//==============================================================================
TempoGuess tempoOf (const float* x, int n, double rate)
{
    TempoGuess out;
    const double seconds = rate > 0 ? n / rate : 0;
    if (seconds < 1.5) return out;   // too short to hear a beat in
    // The onsets: how much louder each 5 ms is than the one before (RMS, rises only).
    const int hop = std::max (8, (int) std::lround (rate * 0.005));
    const int frames = n / hop;
    std::vector<double> level ((size_t) frames), flux ((size_t) frames, 0.0);
    // Each over 20 ms (four hops), so a low note's own waves don't look like hits.
    std::vector<double> hopEnergy ((size_t) frames);
    for (int f = 0; f < frames; ++f)
    {
        double e = 0;
        for (int i = f * hop; i < (f + 1) * hop; ++i) e += (double) x[i] * x[i];
        hopEnergy[(size_t) f] = e;
    }
    for (int f = 0; f < frames; ++f)
    {
        double e = 0;
        for (int k = std::max (0, f - 3); k <= f; ++k) e += hopEnergy[(size_t) k];
        level[(size_t) f] = std::sqrt (e / (4.0 * hop));   // loudness, not its log: a kick counts for more than a hi-hat
    }
    for (int f = 1; f < frames; ++f) flux[(size_t) f] = std::max (0.0, level[(size_t) f] - level[(size_t) f - 1]);
    const double mean = std::accumulate (flux.begin(), flux.end(), 0.0) / std::max (1, frames);
    for (auto& v : flux) v -= mean;
    const double zero = std::inner_product (flux.begin(), flux.end(), flux.begin(), 0.0);
    if (zero <= 1e-12) return out;

    const double framesPerSecond = rate / hop;
    const int maxLag = std::min (frames * 2 / 3, (int) (framesPerSecond * 4.0) + 2);   // four beats at 60 BPM, or two thirds of the sound
    std::vector<double> acf ((size_t) maxLag + 2, 0.0);
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double sum = 0;
        for (int f = 0; f + lag < frames; ++f) sum += flux[(size_t) f] * flux[(size_t) f + lag];
        acf[(size_t) lag] = sum / zero * frames / std::max (1, frames - lag);   // each lag's overlap counted alike
    }
    auto at = [&] (double lag)
    {
        if (lag < 1 || lag >= maxLag) return 0.0;
        const int i = (int) lag;
        const double t = lag - i;
        return acf[(size_t) i] * (1 - t) + acf[(size_t) i + 1] * t;
    };
    // Each tempo scored by its beat and the beats after, leaning towards 120.
    double bestScore = -1e9, bestBpm = 0, bestRaw = 0;
    for (double bpm = 60.0; bpm <= 200.0; bpm += 0.05)
    {
        const double lag = framesPerSecond * 60.0 / bpm;
        double sum = 0;
        int k = 0;
        for (int m = 1; m <= 4; ++m)
            if (lag * m < maxLag) { sum += at (lag * m); ++k; }
        if (k == 0) continue;
        const double raw = sum / std::max (k, 2);   // a beat heard only once in the sound is half believed
        const double lean = std::exp (-0.5 * std::pow (std::log2 (bpm / 120.0) / 0.9, 2));
        const double score = raw * (0.6 + 0.4 * lean);
        if (score > bestScore) { bestScore = score; bestBpm = bpm; bestRaw = raw; }
    }
    if (bestRaw < 0.12) return out;   // no steady beat
    // Twice as fast, when the half beat repeats as strongly as the beat: it was the beat.
    while (bestBpm * 2 <= 200.0)
    {
        const double lag = framesPerSecond * 60.0 / bestBpm;
        if (at (lag / 2) < 0.8 * at (lag)) break;
        bestBpm *= 2;
    }
    out.bpm = std::round (bestBpm * 10.0) / 10.0;
    // A loop: its length a whole number of bars (of four beats) at about that tempo.
    for (double factor : { 1.0, 2.0, 0.5 })
        for (double bars : { 1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 0.5 })
        {
            const double exact = bars * 4.0 * 60.0 / seconds;
            const double guess = bestBpm * factor;
            if (exact < 60 || exact > 200) continue;
            if (std::abs (exact - guess) / guess <= 0.03) { out.bpm = std::round (exact * 100.0) / 100.0; out.loop = true; return out; }
        }
    return out;
}

int noteOf (const float* x, int n, double rate, double* centsOut)
{
    if (centsOut) *centsOut = 0;
    const int window = std::max (256, (int) std::lround (rate * 0.046));   // 2048 at 44.1 kHz: down to 40 Hz
    if (n < window || rate <= 0) return -1;
    // Up to 16 windows from the first 4 seconds, those within 30 dB of the loudest.
    const int span = std::min (n, (int) (rate * 4.0));
    const int count = std::max (1, std::min (16, span / window));
    std::vector<int> starts;
    std::vector<double> energies;
    for (int k = 0; k < count; ++k)
    {
        const int at = (int) ((long long) (span - window) * k / std::max (1, count - 1));
        double e = 0;
        for (int i = at; i < at + window; ++i) e += (double) x[i] * x[i];
        starts.push_back (at);
        energies.push_back (e);
    }
    const double loudest = *std::max_element (energies.begin(), energies.end());
    if (loudest <= 1e-9) return -1;
    std::vector<double> notes;
    int tried = 0;
    for (size_t k = 0; k < starts.size(); ++k)
    {
        if (energies[k] < loudest * 0.001) continue;
        ++tried;
        const double hz = pitchOf (x + starts[k], window, rate, 0.12);
        if (hz > 0) notes.push_back (69.0 + 12.0 * std::log2 (hz / 440.0));
    }
    if (tried == 0 || notes.size() * 2 < (size_t) tried) return -1;   // mostly without a pitch
    auto sorted = notes;
    std::sort (sorted.begin(), sorted.end());
    const double median = sorted[sorted.size() / 2];
    // Most of the pitched windows must agree, within a semitone.
    const auto agree = std::count_if (notes.begin(), notes.end(), [median] (double v) { return std::abs (v - median) <= 0.5; });
    if (agree * 10 < (long) notes.size() * 6) return -1;
    double sum = 0;
    for (double v : notes) if (std::abs (v - median) <= 0.5) sum += v;
    const double exact = sum / (double) agree;
    const int note = (int) std::lround (exact);
    if (note < 0 || note > 127) return -1;
    if (centsOut) *centsOut = (exact - note) * 100.0;
    return note;
}

HitShape hitShape (const float* x, int n, double rate)
{
    HitShape h;
    if (x == nullptr || n <= 0 || rate <= 0) return h;
    n = std::min (n, (int) (rate * 10.0));
    // The loudest of each millisecond.
    const int w = std::max (1, (int) (rate / 1000.0));
    const int frames = (n + w - 1) / w;
    std::vector<float> env ((size_t) frames, 0.0f);
    for (int i = 0; i < n; ++i) env[(size_t) (i / w)] = std::max (env[(size_t) (i / w)], std::abs (x[i]));
    const float top = *std::max_element (env.begin(), env.end());
    if (top < 1.0e-4f) return h;
    int onset = 0;
    while (onset < frames && env[(size_t) onset] < 0.1f * top) ++onset;
    int risen = onset;
    while (risen < frames && env[(size_t) risen] < 0.9f * top) ++risen;
    h.onsetMs = onset * 1000.0 * w / rate;
    h.attackMs = (risen - onset) * 1000.0 * w / rate;
    // The last tenth of the sound, against its loudest.
    const int tailFrom = std::max (onset, frames - std::max (1, frames / 10));
    double sum = 0;
    for (int f = tailFrom; f < frames; ++f) sum += env[(size_t) f];
    const double tail = sum / std::max (1, frames - tailFrom);
    h.tailDb = tail > 1.0e-9 ? 20.0 * std::log10 (tail / top) : -120.0;
    // Brightness: zero crossings a second, just after it starts.
    const int from = onset * w, to = std::min (n, from + (int) (rate * 0.05));
    int crossings = 0;
    for (int i = from + 1; i < to; ++i) if ((x[i - 1] < 0) != (x[i] < 0)) ++crossings;
    h.zcr = to - from > 1 ? crossings * rate / (to - from) : 0;
    return h;
}

} // namespace kal::audiotools
