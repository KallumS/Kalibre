/*
    NoteFixer - Note Position & Volume Fixer, built in (decision 0085).

    Older orchestral libraries are sloppily edited: each sample starts with
    the bow or breath before the note, so the attack is heard 30-150 ms after
    the MIDI note, by a different amount on every key and velocity; and some
    notes of an articulation come out louder or quieter than their
    neighbours. This listens to a render of the part, finds where each note
    is really heard and how loud, and says how far to move each note and how
    much to turn it up or down.

    Ported from KallumS/Note-Position-Volume-Fixer (its src/engine.js, the
    measuring its REAPER scripts and Mac app share), keeping its decisions:
    each note measured on its own harmonics, band-passed without delay, after
    the last note's tail is taken away (0002 there); notes starting within
    30 ms are a chord, heard as one attack and moved together (0003); the
    attack is where the note's own sound comes within 12 dB of its first
    peak; loudness is the loudest 50 ms of the note, compared with what its
    velocity, CC1, pitch and (here) articulation predict, fitted so a few
    odd notes can't drag the fit (Huber weights).

    No JUCE in here.
*/

#pragma once

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace kal::notefix
{

struct NoteIn
{
    int pitch = 60;
    int velocity = 100;
    double seconds = 0;        // when the note is sent, in the render's seconds
    double endSeconds = 0;
    int articulation = -1;     // notes of one articulation are compared with each other
    int cc1 = -1;              // the mod wheel in force (dynamics of long notes), -1 none
};

struct Options
{
    bool alignToAttack = true; // false: the very start of any sound
    double attackDb = 12;      // the attack is this far below the note's first peak
    double startDb = 30;       // the start of any sound, this far below
    double maxDelayMs = 400;   // the latest a note's sound can start after it is sent
    double spreadMs = 120;     // how far one note can differ from the part's typical delay
    double minRiseDb = 6;      // a sound must rise this much to count as found
    enum class Method { Auto, Harmonics, Whole } method = Method::Auto;
};

struct Measured
{
    bool found = false;
    double delay = 0;          // seconds from when it was sent to when it is heard
    double onset = 0;          // seconds in the render
    double loudDb = -std::numeric_limits<double>::infinity();
    double deviationDb = std::numeric_limits<double>::quiet_NaN();   // louder (+) or quieter than expected
    int chord = -1;            // notes of one chord share a number
    bool overlapped = false;   // sounds with another note
    bool under = false;        // starts under notes already held
    bool check = false;        // worth a listen: under held notes, at the edge, or unlike its key's others
    std::string measuredOn;    // "harmonics", "whole sound" or "chord"
};

struct Analysis
{
    std::vector<Measured> notes;   // in the order given
    double typicalDelay = 0;       // seconds
    double dbPerVelocity = std::numeric_limits<double>::quiet_NaN();   // fitted, when it makes sense
    bool fitted = false;
    int found = 0;
    double medianDelayMs = 0, spreadMs = 0, loudnessSpreadDb = 0;
    std::vector<std::string> warnings;
};

Analysis analyse (const std::vector<float>& mono, double sampleRate, const std::vector<NoteIn>& notes, const Options& = {});

struct PlanOptions
{
    enum class Timing { EachNote, EachKey, WholePart, Off } timing = Timing::EachNote;
    double timingStrength = 1;
    double targetMs = 0;           // where the sound should land after the written note
    enum class Volume { EachNote, EachKey, Off } volume = Volume::EachNote;
    double volumeStrength = 1;
    double toleranceDb = 1;        // nearer than this to expected: left alone
};

struct NoteFix
{
    bool timed = false;
    double shiftMs = 0;            // move the note this much (earlier: negative)
    bool levelled = false;
    double gainDb = 0;             // turn it up (+) or down by this much
};

std::vector<NoteFix> plan (const Analysis&, const std::vector<NoteIn>&, const PlanOptions& = {});

// MIDI volume (CC7) and expression (CC11) as most players follow them, the
// General MIDI curve: 40 log10 (value / 127) dB.
inline double ccToDb (double v) { return v <= 0 ? -120.0 : 40.0 * std::log10 (v / 127.0); }
int ccForDb (double db);           // the value nearest that level, 0-127

} // namespace kal::notefix
