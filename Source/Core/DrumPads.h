/*
    DrumPads - Drum Pads (decision 0113): 128 pads, one for each MIDI note,
    each a sample or a plug-in (a plug-in wins), with its own volume, pan,
    mute, solo and choke group. Sixteen are shown at a time, four rows of
    four, the lowest note at the bottom left.

    A pad is played by its own note: pad 36 by note 36 (General MIDI's kick).
    Nothing is sequenced inside: clips' notes and keyboards play it, and the
    drum grid opens its clips with a row for each pad that has a sound.

    Only pads that differ from an empty pad are written to the state, so a
    kit of a few sounds stays small.

    No JUCE in here.
*/

#pragma once

#include "DrumKit.h"
#include "Model.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace kal::drumpads
{

inline constexpr int padCount = 128;
inline constexpr int shown = 16;          // four rows of four
inline constexpr int perRow = 4;
inline constexpr int chokeGroups = 8;

struct Pad
{
    std::string name;                     // empty: General MIDI's name for its note, or the note
    std::string sample;                   // a file; empty for none
    PluginRef plugin;                     // a drum synth; no format for none (a plug-in wins over a sample)
    std::string pluginState;              // its settings, base64
    int pluginNote = 60;                  // the note the plug-in is sent
    double volumeDb = 0.0;                // -36 .. +12, as Kalibre Drums' pads
    double pan = 0.0;                     // -1 left .. 1 right
    bool mute = false;
    bool solo = false;
    int choke = 0;                        // pads in the same group (1-8) stop each other

    bool hasPlugin() const { return ! plugin.format.empty(); }
    bool hasSound() const { return hasPlugin() || ! sample.empty(); }
    bool operator== (const Pad&) const = default;
};

struct Kit
{
    std::array<Pad, padCount> pads;       // pads[n] is played by note n
    int view = 36;                        // the lowest of the sixteen shown, a multiple of 4

    bool operator== (const Kit&) const = default;
};

// Empty pads, the view on the kick (36) to the crash (49) and more; the hats (42, 44, 46) one choke group.
Kit standard();

// What a pad is called: its own name, else General MIDI's drum for its note ("Kick"), else the note ("C6").
std::string nameOf (const Kit&, int pad);
// The note as Kalibre writes it everywhere: middle C (60) is C4.
std::string noteName (int note);
// Heard: not muted, and soloed when any pad is.
bool anySolo (const Kit&);
bool heard (const Kit&, int pad);
// The first pad of a view that shows `pad`, or `view` kept in range: 0, 4 .. 112.
int viewFor (int pad);
int clampView (int view);
// The pads that have a sound, lowest first.
std::vector<int> sounding (const Kit&);

std::string toJson (const Kit&);
std::optional<Kit> fromJson (const std::string&);

// For the drum grid and Random Samples: the given pads as a Kalibre Drums kit (name and note each).
drumkit::Kit asDrumKit (const Kit&, const std::vector<int>& pads);
// A drum sound for each of the given pads from the index, of the kind its name says (0106);
// empty where none fits; a new choice each seed.
std::vector<std::string> randomSamples (const Kit&, const std::vector<int>& pads, const library::Index&, unsigned seed);

} // namespace kal::drumpads
