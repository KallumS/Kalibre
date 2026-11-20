/*
    ControlSettings - how this Mac's other gear is connected (decisions 0034,
    0035): where MIDI clock and timecode go, which clock to follow, Link, the
    control surface's ports and OSC. Kept with the app, not the project: the
    same studio opens every song.

    No JUCE in here.
*/

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace kal
{

struct ControlSettings
{
    // Sync (0034)
    std::vector<std::string> clockOut;   // MIDI output ids that get clock and song position
    bool sendClock = true;
    bool sendMtc = false;
    int mtcRate = 1;                     // 0 24, 1 25, 2 29.97 drop, 3 30
    std::string followClock;             // a MIDI input id to follow, or empty
    bool link = false;
    bool linkStartStop = true;
    std::string followMtc;               // a MIDI input id whose timecode Kalibre chases (decision 0043)
    bool sendMmc = false;                // MIDI Machine Control out, to the ports ticked for clock
    bool followMmc = false;              // obey MMC from any input: play, stop, locate, record
    bool networkMidi = false;            // the Mac's network MIDI session, on (decision 0043)

    // Control surfaces (0035)
    std::string mackieIn, mackieOut;     // MIDI ids for a Mackie Control (or compatible) surface
    std::string huiIn, huiOut;           // and for a HUI surface (decision 0043)
    bool oscOn = false;
    int oscPort = 8000;                  // Kalibre listens here
    std::string oscReplyHost = "127.0.0.1";
    int oscReplyPort = 9000;             // and answers here

    // AI agents, through the Model Context Protocol (decision 0099): off until
    // switched on; then Kalibre listens on this port, on this Mac only.
    bool mcpOn = false;
    int mcpPort = 47100;

    bool operator== (const ControlSettings&) const = default;
};

std::string saveSettings (const ControlSettings&);
std::optional<ControlSettings> loadSettings (const std::string&);

} // namespace kal
