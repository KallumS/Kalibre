/*
    ActivityLog - a record of what was done in a song, kept beside it as text
    (decision 0135).

    Record labels ask for evidence that a song was made by hand, not by a
    generator. Every undo step (each edit, by its name), every Undo and Redo,
    playing, stopping, recording, opening, saving, and what the message line
    said is a line: when, and what. An AI agent's edits say so. Saving adds the
    lines since the last save to "<Song> Activity Log.txt" beside the song;
    nothing in it is rewritten. File > Keep an Activity Log turns it off.

    No JUCE in here: the times are given as text by whoever adds a line.
*/

#pragma once

#include <string>
#include <vector>

namespace kal::activity
{

enum class Kind { New, Open, Edit, Undo, Redo, Agent, Play, Stop, Record, Message, Save, Log };

// The word the line starts with: "Edit", "Undo", "AI agent", "Message".
const char* word (Kind);

class Log
{
public:
    // A message the same as the one just before it isn't added again.
    void add (std::string when, Kind, std::string what);
    bool empty() const { return pending.empty(); }
    void clear() { pending.clear(); }
    // Every line added since the last take, oldest first, each ending in a new
    // line; they're forgotten, so a save never writes one twice.
    std::string take();

private:
    struct Entry { std::string when; Kind kind; std::string what; };
    std::vector<Entry> pending;
    std::string lastMessage;
};

// What the file says first: what it is, the song, the Kalibre that wrote it,
// and the time zone its times are in.
std::string heading (const std::string& song, const std::string& version, const std::string& utcOffset);

} // namespace kal::activity
