/*
    DrumKit - Kalibre Drums (decision 0105): a kit of pads, each a sample or a
    plug-in (a drum synth), each played by a note; and a pattern of steps the
    device plays with the song, which can also be put on the timeline as a
    clip that the drum grid opens.

    The pattern is the drum grid's own (patterns::Grid): its lanes are the
    pads, one for one, each on its pad's note. So a beat dragged out of the
    device is the same notes the drum grid writes, and opens in it.

    Random Samples chooses a sound for each pad from the musician's scanned
    folders (the Sounds tab's index): a one-shot whose name or folder says
    it's that drum ("Kick 05", "BD_808", "Snares/Tight", "HH Closed", "OHH"),
    else any one-shot not chosen yet.

    No JUCE in here.
*/

#pragma once

#include "Library.h"
#include "Model.h"
#include "Patterns.h"

#include <optional>
#include <string>
#include <vector>

namespace kal::drumkit
{

inline constexpr int maxPads = 16;

struct Pad
{
    std::string name;                // "Kick"
    int note = 36;                   // the note that plays it (General MIDI's drums by default)
    std::string sample;              // a file; empty for none
    PluginRef plugin;                // a drum synth; no format for none (a plug-in wins over a sample)
    std::string pluginState;         // its settings, base64
    int pluginNote = 60;             // the note the plug-in is sent
    double volumeDb = 0.0;
    double pan = 0.0;                // -1 left .. 1 right
    bool mute = false;
    int choke = 0;                   // pads in the same group (1-4) stop each other: a closed hat stops the open one

    bool hasPlugin() const { return ! plugin.format.empty(); }
    bool operator== (const Pad&) const = default;
};

struct Kit
{
    std::vector<Pad> pads;
    patterns::Grid pattern;          // a lane a pad, in the same order, on its note
    bool playsPattern = true;        // the device plays its pattern while the song plays

    bool operator== (const Kit& o) const;
};

// Eight pads: Kick, Snare, Clap, Closed Hat, Open Hat (the hats one choke
// group), Low Tom, High Tom, Crash; a bar of sixteenths, empty.
Kit standard();
// The pattern's lanes made to match the pads: one each, on its note, as many steps as the pattern has.
void matchLanes (Kit&);
// A pad added (a name, the next free General MIDI drum note) or taken out, its lane with it.
int addPad (Kit&, const std::string& name = {});
void removePad (Kit&, int index);

std::string toJson (const Kit&);
std::optional<Kit> fromJson (const std::string&);

// What kind of drum a pad's name, or a sound's name and folders, says.
enum class Kind { Other, Kick, Snare, Clap, ClosedHat, OpenHat, Hat, Tom, Crash, Ride, Cymbal, Rim, Shaker, Percussion };
Kind kindOf (const std::string& nameOrPath);
// A one-shot: a readable audio file, short, not a loop.
bool isOneShot (const library::Item&);
// A sound for each pad from the index (empty where none fits); a new choice each seed.
std::vector<std::string> randomSamples (const Kit&, const library::Index&, unsigned seed);

// Notes (a Good Idea groove, General MIDI drums) as the kit's pattern: each on
// its own pad's note, else on the pad whose kind is nearest (a ride on the
// hats when there's no ride, any tom on a tom); notes no pad takes are left out.
void patternFrom (Kit&, const std::vector<Note>&);
// The pad a note plays: its own, else the nearest kind; -1 for none.
int padFor (const Kit&, int note);

// The pattern as a clip's notes (made by the drum grid, so it opens there), from 0, and how long.
MidiData patternNotes (const Kit&);
Tick patternLength (const Kit&);

} // namespace kal::drumkit
