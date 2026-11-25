#include "ActivityLog.h"

namespace kal::activity
{

const char* word (Kind k)
{
    switch (k)
    {
        case Kind::New:     return "New";
        case Kind::Open:    return "Open";
        case Kind::Edit:    return "Edit";
        case Kind::Undo:    return "Undo";
        case Kind::Redo:    return "Redo";
        case Kind::Agent:   return "AI agent";
        case Kind::Play:    return "Play";
        case Kind::Stop:    return "Stop";
        case Kind::Record:  return "Record";
        case Kind::Message: return "Message";
        case Kind::Save:    return "Save";
        case Kind::Log:     return "Log";
    }
    return "";
}

void Log::add (std::string when, Kind kind, std::string what)
{
    if (kind == Kind::Message)
    {
        if (what == lastMessage) return;
        lastMessage = what;
    }
    else lastMessage.clear();
    for (auto& c : what) if (c == '\n' || c == '\r') c = ' ';
    pending.push_back ({ std::move (when), kind, std::move (what) });
}

std::string Log::take()
{
    std::string out;
    for (auto& e : pending)
    {
        std::string kind = word (e.kind);
        if (kind.size() < 9) kind.append (9 - kind.size(), ' ');
        out += e.when + "  " + kind + " " + e.what + "\n";
    }
    pending.clear();
    return out;
}

std::string heading (const std::string& song, const std::string& version, const std::string& utcOffset)
{
    return "Activity log of \"" + song + "\", written by Kalibre " + version + ".\n"
           "Each line is something done in the song: when (this computer's time, " + utcOffset + "), then what.\n"
           "Edit is a change made by hand, AI agent one made by an AI agent; Message is what Kalibre said.\n"
           "Lines are added each time the song is saved; none are changed or taken away.\n\n";
}

} // namespace kal::activity
