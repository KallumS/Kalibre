/*
    DawProject - the open DAWproject format (Bitwig and PreSonus's, read by
    Bitwig Studio, Studio One, Cubase and others), in and out (decision 0077).

    A .dawproject is a zip holding project.xml, metadata.xml, the audio files
    and plug-in states. This part reads and writes the XML; the window does
    the zip (JUCE's), copying audio in and out.

    Carried: tracks, folders (nested), returns and the master, with volume,
    pan, mute, solo, colour, sends and outputs; tempo and time signatures with
    their changes; markers; MIDI clips with their notes and controllers
    (offset and loop kept); audio clips (where in the file, gain, fades);
    plug-ins on each channel. Kalibre's own devices carry their state in the
    file; another program's plug-ins are matched by name, and their state is
    kept in a side file only Kalibre reads (`kalibre/`), as a .vstpreset
    isn't what JUCE saves. Automation and warping too (decision 0084). Not
    yet: takes, versions.

    No JUCE in here.
*/

#pragma once

#include "Model.h"

#include <map>
#include <string>
#include <vector>

namespace kal::dawproject
{

struct Written
{
    std::string projectXml, metadataXml;
    std::vector<std::pair<std::string, std::string>> audio;    // path in the zip -> the clip's file as the project has it
    std::vector<std::pair<std::string, std::string>> states;   // path in the zip -> a device's state (base64, as saved)
    // Automation curves (decision 0087): DAWproject's points are straight or held, so a
    // bent segment goes out as short straight pieces; the curves as they were go in
    // this side file (kalibre/automation.json), for Kalibre to read back. Empty: none.
    std::string kalibreAutomation;
};
inline constexpr const char* kalibreAutomationPath = "kalibre/automation.json";

Written write (const Project&, const std::string& appVersion);

struct Read
{
    bool ok = false;
    std::string error;
    Project project;
    std::vector<std::string> leftOut;                           // what wasn't carried, in words
    std::map<Id, std::string> states;                           // device -> path in the zip of its state
    std::map<Id, std::string> kalibreStates;                    // device -> Kalibre's side file, when the zip has one
};

// `metadataXml` may be empty.
// `kalibreAutomation`: the side file's text when the zip has one.
Read read (const std::string& projectXml, const std::string& metadataXml = {}, const std::string& kalibreAutomation = {});

} // namespace kal::dawproject
