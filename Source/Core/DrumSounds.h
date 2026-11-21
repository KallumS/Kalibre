/*
    DrumSounds - is a sound a drum, and which (decision 0106): for the Sounds
    tab's Drums filter and Kalibre Drums' Random Samples alike.

    Three things are weighed:
      - its name: the file's ("Kick 05", "BD_808", "HH Closed", "OHH"), and
        failing that its folders' ("Snares/Tight"); words that say it isn't a
        drum ("Pad", "Vocal", "Chord", "Riser", "Loop", a tempo) rule it out;
      - its length: a few seconds at most (a cymbal's longer);
      - its shape as a hit (audiotools::hitShape, heard by the scan): it starts
        at once, rises in a few milliseconds and dies away. A sound scanned
        before Kalibre heard shapes is judged from its waveform's columns.
    A sound named as a drum needs only its length (and not to rise slowly); one
    named only by its folder must also be shaped like a hit; one whose name
    says nothing must be shaped like a hit and short, and its kind is then
    heard: low and dull a kick, pitched in the middle a tom, very bright and
    short a closed hat, bright a snare, very bright and long a cymbal.

    No JUCE in here.
*/

#pragma once

#include "Library.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace kal::drumsound
{

enum class Kind { Other, Kick, Snare, Clap, ClosedHat, OpenHat, Hat, Tom, Crash, Ride, Cymbal, Rim, Shaker, Percussion };

// What kind of drum a name ("Closed Hat") or a path's file and folders say; Other when they don't.
Kind kindOf (const std::string& nameOrPath);
// "Kick", "Closed Hat", ...; "Drum" for Other.
std::string nameOf (Kind);
// The kinds a filter chooses between, in order.
const std::vector<Kind>& kinds();

struct Verdict
{
    bool drum = false;
    Kind kind = Kind::Other;   // Other: a drum of no kind its name or sound says
    bool heard = false;        // the kind was heard, not read in its name
};
Verdict of (const library::Item&);

// Verdicts kept, each worked out again only when its sound changes: a list of
// half a million sounds filtered without judging them all each time.
class Cache
{
public:
    Verdict of (const library::Item&);
private:
    struct Entry { std::int64_t modified = 0, size = 0; int analysis = 0; Verdict v; };
    std::unordered_map<std::string, Entry> entries;
};

// Whether a verdict's kind answers for a pad or filter's kind: a closed hat for "Hat", a ride for "Cymbal"...
bool fits (Kind want, Kind got);

} // namespace kal::drumsound
