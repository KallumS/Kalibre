/*
    Patterns - the drum grid, the step sequencer and the Euclidean sequencer.

    All three are ways of looking at the same notes (decision 0014). A clip's
    notes are the only truth: each editor reads them into its grid and writes
    the grid back as notes, so a pattern drawn on the drum grid can be fixed
    in the piano roll and opened again on the grid, and nothing is kept twice.
    Each reads and writes only the notes it made (decision 0059), so they
    all add to one clip without taking over each other's notes.

    No JUCE in here.
*/

#pragma once

#include "Model.h"

#include <string>
#include <vector>

namespace kal::patterns
{

//==============================================================================
// Euclidean rhythms: `pulses` onsets spread as evenly as they go over
// `steps`, rotated. euclid(8, 3) = x..x..x. (the tresillo).
std::vector<bool> euclid (int steps, int pulses, int rotation = 0);

//==============================================================================
// A drum grid: rows of one pitch each, columns of one step each.

struct Lane
{
    std::string name;
    int pitch = 36;
    std::vector<int> velocity;   // per step; 0 is off
    // Per step, as a note's (decision 0061); empty means 100%, 0 and 0.
    std::vector<int> chance, velocityRange;
    std::vector<double> timingMs;
};

struct Grid
{
    Tick stepTicks = PPQ / 4;     // a sixteenth
    int steps = 16;
    double swing = 0;             // 0-0.75: every second step pushed later by this share of a step
    double gate = 0.9;            // note length as a share of a step
    std::vector<Lane> lanes;
};

// General MIDI's name for a drum ("Pedal Hi-Hat" for 44); "Note 90" past its list.
std::string drumName (int pitch);

// General MIDI's drum names for the usual kit, top row first.
std::vector<Lane> standardKit (int steps);

// Reads a clip's notes made by `who` into a grid: a note belongs to the step
// it starts nearest to. Pitches not in `lanes` get rows of their own.
Grid readGrid (const MidiData&, Grid layout, Tick from, NoteFrom who);

// The grid as notes made by `who`, repeated to fill `length`, from `from`.
// Notes made by `who` on pitches the grid has rows for, and inside
// [from, from + length), are replaced; every other note is left alone.
void writeGrid (MidiData&, const Grid&, Tick from, Tick length, unsigned& nextId, NoteFrom who);

// Random Euclidean settings for a drum (decision 0062): steps, hits and
// rotation in the ranges that drum usually plays (a kick few, a hat many,
// a snare turned onto the backbeat); a new set for each seed.
struct EuclidSettings { int steps = 16, pulses = 4, rotation = 0; };
EuclidSettings randomEuclid (int pitch, unsigned seed);

// Fills one row from a Euclidean rhythm.
void euclidLane (Lane&, int steps, int pulses, int rotation, int velocity = 100, int accentEvery = 0);

//==============================================================================
// The step sequencer: one note per step, with its own pitch, velocity and
// gate, a tie to carry it into the next step.

struct Step
{
    bool on = false;
    int pitch = 60;
    int velocity = 100;
    double gate = 0.5;    // share of a step
    bool tie = false;     // runs on into the next step
    int chance = 100;            // decision 0061
    int velocityRange = 0;
    double timingMs = 0;
};

struct StepSequence
{
    Tick stepTicks = PPQ / 4;
    std::vector<Step> steps = std::vector<Step> (16);
    double swing = 0;
};

// As the grid: only the notes made by `who` are read, and replaced.
StepSequence readSteps (const MidiData&, Tick stepTicks, int count, Tick from, NoteFrom who);
void writeSteps (MidiData&, const StepSequence&, Tick from, Tick length, unsigned& nextId, NoteFrom who);

} // namespace kal::patterns
