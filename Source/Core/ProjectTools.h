/*
    ProjectTools - a project's files, templates, tags and interchange (decision 0041).

      - Every audio file a project uses, and rewriting where they are: for
        Collect and Save, archiving and relinking files that moved.
      - Relinking by name: missing files found again in a folder.
      - Templates: a project's tracks, plug-ins and routing without its music.
      - ID3v2 tags for MP3 exports.
      - Final Cut Pro XML (xmeml): the audio clips, for video editors and
        other DAWs (DaVinci Resolve, Premiere, Logic).

    No JUCE in here.
*/

#pragma once

#include "Model.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace kal::projecttools
{

// Each file once, in the order first used (clips, takes, slots, renders, frozen tracks, the video).
std::vector<std::string> referencedFiles (const Project&);
// Every use of a key's file becomes its value.
void replaceFiles (Project&, const std::map<std::string, std::string>&);

// For each missing file, a candidate with the same name (case ignored), if any.
std::map<std::string, std::string> relinkByName (const std::vector<std::string>& missing, const std::vector<std::string>& candidates);
std::string fileNameOf (const std::string& path);

// The project as a template: tracks, plug-ins, sends, routing, tempo and scale;
// with `keepMusic` false, no clips, takes, slots, markers, sections or automation.
Project asTemplate (const Project&, bool keepMusic);

// An ID3v2.3 tag (title, artist, album, year, genre, track, comment, ISRC).
std::vector<unsigned char> id3v2 (const Metadata&);

// Final Cut Pro 7 XML of the audio clips, a track a track. `fileSeconds`
// gives a file's length; `framesPerSecond` the sequence's timebase.
std::string fcpXml (const Project&, std::function<double (const std::string&)> fileSeconds, int framesPerSecond = 25);

} // namespace kal::projecttools
