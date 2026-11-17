/*
    AutoCc - AutoCC in Kalibre (decision 0060).

    AutoCC (KallumS/AutoCC, a REAPER JSFX) plays orchestral CC curves for the
    notes it hears: each note draws an arc - a rise, a settle, a sustain while
    the key is held, a fall - on CC1 (dynamics), CC11 (expression), CC7
    (volume) and CC21 (vibrato), shaped for the instrument family, from Lorne
    Balfe's mock-ups. Here the same engine, step for step (5 ms, as AutoCC
    runs by default), plays a clip's notes and writes what it would send into
    the clip's CC lanes, where they can be seen and redrawn.

    Its presets are by family; the track's name, or its instrument's, says
    which family it is: "Violins 1" is strings, "English Horn" a woodwind.

    No JUCE in here.
*/

#pragma once

#include "Model.h"

#include <array>
#include <string>
#include <vector>

namespace kal::autocc
{

// AutoCC's presets. Ensemble is its Default: every family together.
enum class Family { None, Ensemble, Strings, Brass, Woodwinds };

std::string familyName (Family);   // "strings", "brass", "woodwinds", "the whole orchestra"

// The family an instrument's name says, if it names one AutoCC knows.
Family familyFor (const std::string& name);

// A track's family: from its name, else from its instruments' names.
struct Found
{
    Family family = Family::None;
    std::string from;   // the name it was found in
};
Found familyFor (const Track&);

// One of AutoCC's four fixed lanes.
struct Lane
{
    int cc = 1;
    double floor = 0, peak = 0, sustain = 0;   // 0-127
    double rise = 0, settle = 0, fall = 0;     // ms
    double velocity = 0;                       // % of the swell that follows velocity
    double riseShape = 1, fallShape = 1;       // curve exponents
};
std::array<Lane, 4> preset (Family);

// AutoCC's global settings, at its defaults.
struct Settings
{
    enum class Mode { Gated, OneShot, Adr };
    Mode mode = Mode::Gated;
    bool retrigger = false;    // false: Legato, a new swell only after a break
    double depth = 1.0;        // scales every lane's movement round its floor
    double time = 1.0;         // stretches every stage
    int dynamic = 0;           // -64..64: moves CC1 and CC11 together
    bool pedalHolds = true;    // the sustain pedal (CC64 in the clip) holds the arc
    double holdMs = 800;       // One-Shot's sustain
    double stepMs = 5;         // how often a value is worked out
};

// The CC events AutoCC would send for the clip's notes, in the clip's own
// ticks (as its notes are), from where it starts playing to its end.
std::vector<MidiEvent> render (const Clip&, const TempoMap&, Family, const Settings& = {});

// Writes them into the clip, replacing every event those four lanes had, and
// shows the lanes on its track. Returns the CC numbers written.
std::vector<int> fill (Project&, Id clip, Family, const Settings& = {});

} // namespace kal::autocc
