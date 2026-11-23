/*
    AudioTools - looking at audio and changing it (decision 0036).

    Everything here works on plain arrays of samples, one pointer a channel,
    so the engine can hand it a juce::AudioBuffer and the tests a vector.

      - peak and normalise
      - where a recording is silent, for Strip Silence
      - where the hits are (transients), from the rise of energy in short frames
      - audio quantise: warp markers that move each hit onto the grid
      - sample edits on a range: gain, silence, reverse, fades, invert, DC
      - a spectrum (FFT) and the pitch of a note (YIN), for the spectral view,
        the analyser and the tuner

    No JUCE in here.
*/

#pragma once

#include "Model.h"

#include <complex>
#include <vector>

namespace kal::audiotools
{

struct Span { int start = 0, end = 0; bool operator== (const Span&) const = default; };

//==============================================================================
float peak (const float* const* channels, int numChannels, int start, int end);

// The sample nearest `at` (within `reach` samples) where the channels, mixed,
// cross zero or are zero: the one of the two either side nearer to zero.
// `at` itself when there's none in reach (decision 0054).
int nearestZeroCrossing (const float* const* channels, int numChannels, int numSamples, int at, int reach);
// The gain (dB) that brings `peakValue` to `targetDb` (0 dBFS less a margin).
double normaliseGainDb (float peakValue, double targetDb = -0.3);

//==============================================================================
// Stretches that aren't silent: louder than `thresholdDb` somewhere within,
// silences shorter than `minSilence` seconds ignored, each widened by `pad` seconds.
std::vector<Span> sounding (const float* const* channels, int numChannels, int numSamples, double rate,
                            double thresholdDb = -48.0, double minSilence = 0.15, double pad = 0.01);

// Sample positions of the hits. `sensitivity` 0..1: higher finds quieter hits.
std::vector<int> transients (const float* const* channels, int numChannels, int numSamples, double rate,
                             double sensitivity = 0.5);

// Every moment that could be a hit at the highest sensitivity, with how much
// it rises and how loud it is, so a sensitivity can be chosen later without
// looking again (Kalibre Sampler's slices, on the audio thread, 0095).
struct HitCandidate { int at = 0; int frame = 0; double rise = 0, levelDb = -120; };
std::vector<HitCandidate> hitCandidates (const float* const* channels, int numChannels, int numSamples, double rate);
// The frames between hits at least this far apart, at this rate.
int hitGapFrames (double rate);
// Whether a candidate is a hit at `sensitivity`, given the frame of the last one taken.
bool isHit (const HitCandidate&, double sensitivity, int lastFrame, int gapFrames);
// The hits among candidates at a sensitivity, as `transients` finds them: quick to ask
// again and again, so a slider can show how many as it moves (Split at Transients' Sensitivity).
std::vector<int> hitsAt (const std::vector<HitCandidate>&, double sensitivity, double rate);

//==============================================================================
// Where a moment of the file plays, in ticks from the clip's content start,
// and back (decision 0022): through the markers, the file's tempo, or the song's.
double sourceSecondsAt (const AudioData&, Tick contentTick);   // -1 when it plays at its own speed
Tick tickForSource (const AudioData&, double sourceSeconds, const TempoMap&, Tick contentStart);

// Warp markers that put every hit (seconds into the file) on the grid.
// `strength` 0..1: how far towards the grid line each moves. The clip's
// first and last moments are pinned so nothing else shifts.
std::vector<WarpMarker> quantiseMarkers (const AudioData&, const std::vector<double>& hitSeconds,
                                         const TempoMap&, Tick contentStart, Tick contentLength,
                                         Tick grid, double strength = 1.0);

//==============================================================================
// Sample edits on [start, end) of every channel.
void gain (float* const* channels, int numChannels, int start, int end, float gainFactor);
void silence (float* const* channels, int numChannels, int start, int end);
void reverse (float* const* channels, int numChannels, int start, int end);
void fade (float* const* channels, int numChannels, int start, int end, bool in);
void invert (float* const* channels, int numChannels, int start, int end);
void removeDc (float* const* channels, int numChannels, int start, int end);

//==============================================================================
// In place, size a power of two.
void fft (std::vector<std::complex<float>>&, bool inverse = false);
// Magnitudes (dB) of the first n/2 bins of n samples under a Hann window.
std::vector<float> spectrumDb (const float* samples, int n);

// The pitch in Hz of a single note (YIN), or 0 when there's none.
double pitchOf (const float* samples, int n, double rate, double threshold = 0.15);
// The nearest note to a frequency, and how far off it is in cents.
void nearestNote (double hz, int& midiNote, double& cents, double a4 = 440.0);

//==============================================================================
// What the Sounds browser's scan hears in a sample (decision 0095), from one
// channel (the channels mixed).

// The tempo it was played at, from how its hits repeat (the onsets'
// autocorrelation over 60 to 200 BPM, leaning towards 120): 0 when it has no
// steady beat. `loop` when its length is a whole number of bars at that tempo
// (within 3%), which is then exact: a loop is cut to its bars. Only a loop's
// tempo is trusted enough to stretch it.
struct TempoGuess { double bpm = 0; bool loop = false; };
TempoGuess tempoOf (const float* mono, int n, double rate);

// The note it plays most (YIN on stretches of its sounding part, their
// median): -1 when most of it has no one pitch (drums, chords, noise).
int noteOf (const float* mono, int n, double rate, double* cents = nullptr);

// The shape of a hit (decision 0106), for telling drums from other sounds:
// when it starts (the first millisecond at a tenth of its loudest), how long
// it takes to rise from there to nine tenths of it, how loud its last tenth
// is against its loudest (dB; very negative when it dies away), and how bright
// it is (zero crossings a second in the 50 ms after it starts: a kick's a few
// hundred, a snare's thousands, a hat's ten thousand and more). Up to the first
// 10 seconds are looked at. onsetMs is -1 for silence.
struct HitShape { double onsetMs = -1, attackMs = -1, tailDb = 0, zcr = -1; };
HitShape hitShape (const float* mono, int n, double rate);

} // namespace kal::audiotools
