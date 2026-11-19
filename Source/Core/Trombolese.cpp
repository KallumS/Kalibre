#include "Trombolese.h"

#include <algorithm>
#include <cmath>

namespace kal::trombolese
{

namespace
{
constexpr double pi = 3.14159265358979323846;

// Made by KallumS/Trombolese's own functions (scripts in decision 0093):
// tune_to_waveguide(pitch_neutral(Trombolese(), regime=3, n_alpha=9), regime=3,
// n_alpha=9, sample_rate) for the lengths, _fit_mouth_reflection of the bore at
// alpha 0 for the bell. One row per sample rate.
struct Table { double rate; std::array<double, 9> alphas, lengths; double targetF1, gain, pole; };
#include "TromboleseTables.inc"
#include "TromboleseNotes.inc"

double lerp (double t, double a, double b) { return (1.0 - t) * a + t * b; }

// numpy.interp: x clamped to the ends; a step (a repeated x) is taken from its right.
double interp (double x, const double* xs, const double* fs, int count)
{
    if (x <= xs[0]) return fs[0];
    if (x >= xs[count - 1]) return fs[count - 1];
    const int j = (int) (std::upper_bound (xs, xs + count, x) - xs) - 1;
    const double span = xs[j + 1] - xs[j];
    return span == 0 ? fs[j] : fs[j] + (x - xs[j]) * (fs[j + 1] - fs[j]) / span;
}
} // namespace

NoteSetting settingFor (int note, double alpha, int* sounded)
{
    const int row = alpha < 0.25 ? 0 : alpha < 0.75 ? 1 : 2;
    auto playable = [&] (int m) { return m >= lowestNote && m <= highestNote && noteSettings[row][m - lowestNote].centsOff < 50; };
    int m = note;
    while (m < lowestNote) m += 12;
    while (m > highestNote) m -= 12;
    for (int k = 0; k < 4 && ! playable (m); ++k) m += m + 12 <= highestNote ? 12 : -12;
    if (sounded) *sounded = m;
    return noteSettings[row][std::clamp (m, lowestNote, highestNote) - lowestNote];
}

double Air::speedOfSound() { return 331.45 * std::sqrt (1.0 + 20.0 / 273.15); }
double Air::density() { return 1.2929 * 273.15 / (273.15 + 20.0); }
double Air::kinematicViscosity() { return 1.708e-5 * (1.0 + 0.0029 * 20.0) / density(); }

Geometry Geometry::forRate (double sampleRate)
{
    const Table* best = &tables[0];
    for (const auto& t : tables) if (std::abs (t.rate - sampleRate) < std::abs (best->rate - sampleRate)) best = &t;
    Geometry g;
    g.alphas.assign (best->alphas.begin(), best->alphas.end());
    g.lengths.assign (best->lengths.begin(), best->lengths.end());
    g.targetF1 = best->targetF1;
    g.mouthGain = best->gain;
    g.mouthPole = best->pole;
    return g;
}

double Geometry::boreLengthAt (double alpha) const
{
    return interp (alpha, alphas.data(), lengths.data(), (int) alphas.size());
}

int Geometry::profile (double alpha, double slide, std::array<double, 104>& xs, std::array<double, 104>& rs) const
{
    int k = 0;
    double x = 0;
    auto segment = [&] (double rin, double rout, double length)
    {
        xs[(size_t) k] = x; rs[(size_t) k] = rin; ++k;
        xs[(size_t) k] = x + length; rs[(size_t) k] = rout; ++k;
        x += length;
    };
    // The mouthpiece: a cup and its throat, becoming a reed staple.
    const double cupR = lerp (alpha, cupRadius, stapleRadius);
    const double cupL = lerp (alpha, cupLength, stapleLength);
    const double throatR = lerp (alpha, cupThroatRadius, coneThroat);
    segment (cupR, cupR, cupL);
    segment (throatR, throatR, cupThroatLength);
    // The bore: one frustum, the slide stretching it.
    const double throat = lerp (alpha, cylThroat, coneThroat);
    const double entry = lerp (alpha, cylEntry, coneEntry);
    segment (throat, entry, boreLengthAt (alpha) + slide);
    // The bell: a power-law horn in conical slices.
    const double ratio = bellRadius / entry;
    const double step = bellLength / bellSegments;
    double previous = entry;
    for (int i = 1; i <= bellSegments; ++i)
    {
        // As numpy.linspace: i times the step, the last exactly 1.
        const double s = i == bellSegments ? 1.0 : i * (1.0 / bellSegments);
        const double r = entry * std::pow (1.0 + (std::pow (ratio, 1.0 / bellFlare) - 1.0) * s, bellFlare);
        segment (previous, r, step);
        previous = r;
    }
    return k;
}

//==============================================================================
void Voice::Valve::tune (double f, double qq, double w, double restOpening, double closing)
{
    frequency = f;
    q = qq;
    width = w;
    rest = restOpening;
    const double omega = 2.0 * pi * f;
    inverseMass = (omega * omega * restOpening) / closing;
}

void Voice::Valve::advance (double dp, double dt)
{
    const double omega = 2.0 * pi * frequency;
    const double acceleration = striking * dp * inverseMass - (omega / q) * velocity - omega * omega * (opening - rest);
    velocity += acceleration * dt;
    opening += velocity * dt;
    if (opening < 0.0)
    {
        opening = 0.0;
        if (velocity < 0.0) velocity = 0.0;
    }
    else if (opening > 3.0 * rest)
    {
        opening = 3.0 * rest;
        if (velocity > 0.0) velocity = 0.0;
    }
}

Voice::Voice (const Geometry& g, double sampleRate) : geometry (g), rate (sampleRate)
{
    dx = Air::speedOfSound() / rate;
    // Room for the longest bore any control can ask for: no allocation while playing.
    const double longest = geometry.cupLength + geometry.cupThroatLength + geometry.bellLength
                           + *std::max_element (geometry.lengths.begin(), geometry.lengths.end()) * 2.6 + 2.0;
    maxSections = (int) std::ceil (longest / dx) + 2;
    for (auto* v : { &radii, &reflections, &losses, &dispersion, &forward, &backward, &dispForward, &dispBackward,
                     &nextForward, &nextBackward, &scratch })
        v->reserve ((size_t) maxSections);
    // As Voice.__post_init__: the ladder made at alpha 0, slide 0; its mouth kept from then.
    section (0.0, 0.0, false);
    forward.assign ((size_t) n, 0.0);
    backward.assign ((size_t) n, 0.0);
    dispForward.assign ((size_t) n, 0.0);
    dispBackward.assign ((size_t) n, 0.0);
    nextForward.assign ((size_t) n, 0.0);
    nextBackward.assign ((size_t) n, 0.0);
    const double correction = 2.0 * 0.6133 * radii[(size_t) n - 1] / dx - 1.0;
    delaySamples = std::max (correction, 0.0);
    delay.assign ((size_t) std::ceil (delaySamples) + 2, 0.0);
    setMorph (0.0, 60.0);
    reset();
}

void Voice::reset()
{
    std::fill (forward.begin(), forward.end(), 0.0);
    std::fill (backward.begin(), backward.end(), 0.0);
    std::fill (dispForward.begin(), dispForward.end(), 0.0);
    std::fill (dispBackward.begin(), dispBackward.end(), 0.0);
    std::fill (delay.begin(), delay.end(), 0.0);
    mouthState = 0;
    write = 0;
    lip.opening = lip.rest; lip.velocity = 0;
    reed.opening = reed.rest; reed.velocity = 0;
    dcState = previousRadiated = 0;
}

// section_bore: the geometry resampled onto one-sample sections, with each one's
// reflection, wall loss and dispersion; the ladder resampled onto the new count.
void Voice::section (double alpha, double slide, bool adopt)
{
    alpha = std::clamp (alpha, 0.0, 1.0);
    slide = std::max (slide, -0.95 * geometry.boreLengthAt (alpha));
    std::array<double, 104> xs {}, rs {};
    const int count = geometry.profile (alpha, slide, xs, rs);
    const double total = xs[(size_t) count - 1];
    const int newN = std::clamp ((int) std::lround (total / dx), 2, maxSections);
    radii.resize ((size_t) newN);
    for (int i = 0; i < newN; ++i) radii[(size_t) i] = interp ((i + 0.5) * dx, xs.data(), rs.data(), count);
    reflections.resize ((size_t) newN - 1);
    for (int i = 0; i + 1 < newN; ++i)
    {
        const double a = pi * radii[(size_t) i] * radii[(size_t) i], b = pi * radii[(size_t) i + 1] * radii[(size_t) i + 1];
        reflections[(size_t) i] = (a - b) / (a + b);
    }
    const double c = Air::speedOfSound(), nu = Air::kinematicViscosity();
    const double layer = std::sqrt (nu) + (Air::gamma - 1.0) * std::sqrt (nu / Air::prandtl);
    const double omega = 2.0 * pi * 250.0, omegaRef = 2.0 * pi * 150.0;
    losses.resize ((size_t) newN);
    dispersion.resize ((size_t) newN);
    for (int i = 0; i < newN; ++i)
    {
        const double attenuation = std::sqrt (omega / 2.0) * layer / (radii[(size_t) i] * c);
        losses[(size_t) i] = std::exp (-attenuation * dx);
        const double attenuationRef = std::sqrt (omegaRef / 2.0) * layer / (radii[(size_t) i] * c);
        const double excess = attenuationRef * dx * rate / omegaRef;
        dispersion[(size_t) i] = excess / (1.0 + excess);
    }
    if (adopt && newN != n)
    {
        // Waveguide.adopt: the waves stretched onto the new count, so the note goes on.
        for (auto* v : { &forward, &backward, &dispForward, &dispBackward })
        {
            scratch.resize ((size_t) newN);
            for (int k = 0; k < newN; ++k)
            {
                const double x = newN > 1 ? (double) k / (newN - 1) : 0.0;
                const double pos = x * (n - 1);
                const int i = std::min ((int) pos, n - 2);
                const double f = pos - i;
                scratch[(size_t) k] = (*v)[(size_t) i] + f * ((*v)[(size_t) i + 1] - (*v)[(size_t) i]);
            }
            v->swap (scratch);
        }
        nextForward.resize ((size_t) newN);
        nextBackward.resize ((size_t) newN);
    }
    n = newN;
}

void Voice::setMorph (double b, double frequency)
{
    beta = std::clamp (b, 0.0, 1.0);
    lip.tune (frequency, 15.0, 0.012, 3.0e-4, 12000.0);
    reed.tune (frequency * 1.25, 12.0, 0.010, 3.0e-4, 7000.0);
}

double Voice::reedStep (double mouthPressure, double returning, double impedance)
{
    const double incident = mouthPressure - 2.0 * returning;
    const double area = (1.0 - beta) * lip.area() + beta * reed.area();
    double flow = 0.0;
    if (area > 0.0)
    {
        const double k = 2.0 * area * area / Air::density();
        const double discriminant = (k * impedance) * (k * impedance) + 4.0 * k * std::abs (incident);
        flow = std::copysign (0.5 * (-k * impedance + std::sqrt (discriminant)), incident);
    }
    const double dp = incident - impedance * flow;
    const double dt = 1.0 / rate;
    lip.advance (dp, dt);
    reed.advance (dp, dt);
    return returning + impedance * flow;
}

double Voice::guideStep (double injected)
{
    // Scatter at every junction at once.
    for (int i = 0; i + 1 < n; ++i)
    {
        const double delta = reflections[(size_t) i] * (forward[(size_t) i] - backward[(size_t) i + 1]);
        nextForward[(size_t) i + 1] = forward[(size_t) i] + delta;
        nextBackward[(size_t) i] = backward[(size_t) i + 1] + delta;
    }
    // The mouth: the end correction's delay, then a one-pole reflection.
    const double arriving = forward[(size_t) n - 1];
    const int size = (int) delay.size();
    delay[(size_t) write] = arriving;
    const double read = write - delaySamples;
    const int whole = (int) std::floor (read);
    const double frac = read - whole;
    const double earlier = delay[(size_t) (((whole % size) + size) % size)];
    const double later = delay[(size_t) ((((whole + 1) % size) + size) % size)];
    write = (write + 1) % size;
    const double delayed = earlier + frac * (later - earlier);
    mouthState = geometry.mouthPole * mouthState + (1.0 - geometry.mouthPole) * delayed;
    const double reflected = -geometry.mouthGain * mouthState;
    nextBackward[(size_t) n - 1] = reflected;
    const double radiated = arriving + reflected;
    nextForward[0] = injected;
    // Each section's one-pole (the wall drag that slows the wave), then its loss.
    for (int i = 0; i < n; ++i)
    {
        const double g = dispersion[(size_t) i];
        dispForward[(size_t) i] = dispForward[(size_t) i] * g + (1.0 - g) * nextForward[(size_t) i];
        dispBackward[(size_t) i] = dispBackward[(size_t) i] * g + (1.0 - g) * nextBackward[(size_t) i];
        forward[(size_t) i] = dispForward[(size_t) i] * losses[(size_t) i];
        backward[(size_t) i] = dispBackward[(size_t) i] * losses[(size_t) i];
    }
    return radiated;
}

void Voice::block (double* out, int count, const Controls& c)
{
    // Re-sectioned only when the controls moved (Voice._apply_geometry's key).
    const double a = std::round (c.alpha * 1e4) / 1e4, s = std::round (c.slide * 1e5) / 1e5;
    if (! haveKey || a != keyAlpha || s != keySlide)
    {
        haveKey = true;
        keyAlpha = a;
        keySlide = s;
        section (c.alpha, c.slide, true);
    }
    const double impedance = Air::density() * Air::speedOfSound() / (pi * radii[0] * radii[0]);
    setMorph (c.beta, c.lipFrequency);
    for (int i = 0; i < count; ++i)
    {
        const double injected = reedStep (c.pressure, backward[0], impedance);
        const double radiated = guideStep (injected);
        // Radiation goes as the wave's derivative; a leaky integrator takes out the reed's DC.
        const double differentiated = radiated - previousRadiated;
        previousRadiated = radiated;
        dcState = 0.995 * dcState + differentiated;
        out[i] = dcState;
    }
}

void Voice::render (double* out, int count, const Controls& c)
{
    for (int start = 0; start < count; start += blockSize)
        block (out + start, std::min (blockSize, count - start), c);
}

} // namespace kal::trombolese
