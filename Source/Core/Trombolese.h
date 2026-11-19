/*
    Trombolese - a physical-modelling wind instrument that crosses a trombone
    with an oboe and morphs its bore between them while it plays (decision
    0093). KallumS/Trombolese is the authority: its Python voice (synth.py,
    waveguide.py, reed.py, the geometry in bore.py) ported here operation for
    operation, and checked against renders the Python made
    (Tests/TestTrombolese.cpp).

    What the Python works out offline and isn't redone here: the bore lengths
    that hold the third regime's pitch across the morph (tune_to_waveguide of
    pitch_neutral, regime 3), and the bell mouth's one-pole reflection fit, both
    per sample rate. They are tables in Trombolese.cpp, made by the repo's own
    functions.

    The voice: a Kelly-Lochbaum ladder one sample per section, a morphing
    two-valve reed, controls read every 64 samples. Nothing allocates once
    made (the audio thread plays it).

    No JUCE in here.
*/

#pragma once

#include <algorithm>
#include <array>
#include <vector>

namespace kal::trombolese
{

struct Controls
{
    double pressure = 0;        // breath, Pa
    double lipFrequency = 60;   // the embouchure, Hz: which regime speaks
    double slide = 0;           // extra tubing, m (negative shortens)
    double alpha = 0;           // the bore: cylinder 0 to cone 1
    double beta = 0;            // the reed: lips 0 to double reed 1
};

// The geometry (bore.py's Trombolese, its defaults), pitch-compensated for a sample rate.
struct Geometry
{
    double cylThroat = 0.00695, cylEntry = 0.00695, coneThroat = 0.00200, coneEntry = 0.01500;
    double bellLength = 0.60, bellRadius = 0.1080, bellFlare = 3.5;
    static constexpr int bellSegments = 48;
    double cupRadius = 0.01250, cupLength = 0.02000, cupThroatRadius = 0.00350, cupThroatLength = 0.02500;
    double stapleRadius = 0.00230, stapleLength = 0.02500;
    std::vector<double> alphas, lengths;   // the compensation: bore length against alpha
    double targetF1 = 0;                   // the regime held, Hz
    double mouthGain = 0, mouthPole = 0;   // the bell's reflection fit at this rate

    static Geometry forRate (double sampleRate);   // the nearest rate the tables were made at
    double boreLengthAt (double alpha) const;
    // The bore's radius against distance from the lips: steps repeat an x.
    int profile (double alpha, double slide, std::array<double, 104>& xs, std::array<double, 104>& rs) const;
};

// Air at 20 C (constants.py).
struct Air
{
    static double speedOfSound();
    static double density();
    static double kinematicViscosity();
    static constexpr double gamma = 1.4017, prandtl = 0.708;
};

// How to sound a MIDI note, as a trombonist knows a partial and a slide position:
// the embouchure (times the note's frequency) and the slide, found by playing
// each note (tools/TromboleseCalibrate.cpp) at the bore position nearest
// `alpha`. A note the bore can't sound there takes the nearest octave it can.
struct NoteSetting { double lipRatio, slide, level, centsOff; };
NoteSetting settingFor (int note, double alpha, int* sounded = nullptr);
constexpr int lowestNote = 34, highestNote = 84;

class Voice
{
public:
    Voice (const Geometry&, double sampleRate);
    void reset();
    // Renders n samples at these controls, or with automation(position 0-1) giving
    // them once a block, as Voice.render does.
    void render (double* out, int n, const Controls&);
    template <typename F> void renderAutomated (double* out, int n, F automation)
    {
        for (int start = 0; start < n; start += blockSize)
        {
            const Controls c = automation ((double) start / (double) std::max (n - 1, 1));
            block (out + start, std::min (blockSize, n - start), c);
        }
    }
    void block (double* out, int n, const Controls&);   // n <= blockSize: one block's controls
    static constexpr int blockSize = 64;
    int sections() const { return n; }

private:
    Geometry geometry;
    double rate;
    double dx;
    int maxSections;
    // The sectioned bore.
    int n = 0;
    std::vector<double> radii, reflections, losses, dispersion;
    // The ladder.
    std::vector<double> forward, backward, dispForward, dispBackward, nextForward, nextBackward, scratch;
    double mouthState = 0;
    std::vector<double> delay;
    double delaySamples = 0;
    int write = 0;
    // The reed: two valves.
    struct Valve
    {
        double striking, frequency = 100, q = 10, width = 0.010, rest = 3.0e-4, inverseMass = 0, opening = 3.0e-4, velocity = 0;
        void tune (double f, double q, double w, double rest, double closing);
        void advance (double dp, double dt);
        double area() const { return width * (opening > 0 ? opening : 0); }
    };
    Valve lip { 1.0 }, reed { -1.0 };
    double beta = 0;
    double dcState = 0, previousRadiated = 0;
    bool haveKey = false;
    double keyAlpha = 0, keySlide = 0;

    void section (double alpha, double slide, bool adopt);
    void setMorph (double beta, double frequency);
    double reedStep (double mouthPressure, double returning, double impedance);
    double guideStep (double injected);
};

} // namespace kal::trombolese
