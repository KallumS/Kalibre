// AI agents through MCP (decision 0099): the tools, the server on this Mac, the bridge.
#include "Check.h"
#include "AgentTools.h"
#include "BuiltinInstruments.h"
#include "Theme.h"
#include "Edit.h"
#include "McpServer.h"

#include <sstream>
#include <thread>

using namespace kal;
using namespace juce;
using json = nlohmann::json;

namespace
{
struct Studio
{
    PluginManager plugins;
    Engine engine { plugins };
    Controller c { engine };
    std::unique_ptr<mcp::Server> server = makeAgentServer (c);
    int id = 0;
    Studio() { c.project = Project(); c.refresh(); }

    json call (const std::string& tool, json args = json::object())
    {
        const auto reply = json::parse (server->handle (json ({ { "jsonrpc", "2.0" }, { "id", ++id }, { "method", "tools/call" },
                                                                { "params", { { "name", tool }, { "arguments", args } } } }).dump()));
        return reply["result"];
    }
    // A tool's text, parsed as JSON when it is.
    json result (const std::string& tool, json args = json::object())
    {
        auto r = call (tool, args);
        const std::string text = r["content"][0]["text"];
        CHECK (! r["isError"].get<bool>());
        if (r["isError"].get<bool>()) std::printf ("    %s: %s\n", tool.c_str(), text.c_str());
        const auto j = json::parse (text, nullptr, false);
        return j.is_discarded() ? json (text) : j;
    }
};
} // namespace

TEST ("agents: an agent makes a track with an instrument, writes a clip of notes, changes them, and each change is an undo step (0099)")
{
    Studio s;
    // What it's offered.
    const auto list = json::parse (s.server->handle (R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})"));
    StringArray names;
    for (auto& t : list["result"]["tools"]) names.add (String (t["name"].get<std::string>()));
    for (auto* n : { "get_song", "create_track", "add_plugin", "create_midi_clip", "add_notes", "clip_action", "transport", "run_script", "export_mix" })
        CHECK (names.contains (n));

    const auto before = s.c.history.size();
    auto track = s.result ("create_track", { { "name", "Bass" }, { "instrument", "Kalibre Synth" } });
    const Id t = track["id"];
    CHECK_EQ (String (s.c.project.track (t)->name), String ("Bass"));
    CHECK_EQ (s.c.project.track (t)->devices.size(), (size_t) 1);
    CHECK_EQ (track["devices"][0]["name"].get<std::string>(), std::string ("Kalibre Synth"));

    auto clip = s.result ("create_midi_clip", { { "track", "bass" }, { "start_beat", 4 }, { "length_beats", 4 }, { "name", "Riff" } });
    const Id cl = clip["id"];
    CHECK_EQ (s.c.project.clip (cl)->start, PPQ * 4);
    s.result ("add_notes", { { "clip", cl }, { "notes", { { { "pitch", 40 }, { "start_beat", 0 }, { "length_beats", 1 } },
                                                          { { "pitch", 43 }, { "start_beat", 1.5 }, { "length_beats", 0.5 }, { "velocity", 90 } } } } });
    auto got = s.result ("get_clip", { { "clip", cl } });
    CHECK_EQ (got["notes"].size(), (size_t) 2);
    CHECK_EQ (got["notes"][1]["pitch"].get<int>(), 43);
    CHECK_EQ (got["notes"][1]["start_beat"].get<double>(), 1.5);
    s.result ("clip_action", { { "clip", cl }, { "action", "transpose" }, { "semitones", 12 } });
    CHECK_EQ (s.c.project.clip (cl)->midi.notes[0].pitch, 52);
    s.result ("update_track", { { "track", t }, { "volume_db", -6 }, { "mute", true } });
    CHECK_EQ (s.c.project.track (t)->volumeDb, -6.0);
    CHECK (s.c.project.track (t)->mute);

    // A plug-in, by name, and its parameters.
    auto dev = s.result ("add_plugin", { { "track", t }, { "plugin", "kalibre sampler" } });
    const Id d = dev["id"];
    CHECK_EQ (s.c.project.track (t)->devices.size(), (size_t) 2);
    auto params = s.result ("get_plugin_parameters", { { "device", d }, { "search", "root" } });
    CHECK_EQ (params.size(), (size_t) 1);
    s.result ("set_plugin_parameter", { { "device", d }, { "parameter", "Root Note" }, { "value", 0.5 } });
    CHECK (std::abs (s.c.engine.instance (d)->getParameters()[0]->getValue() - 0.5f) < 0.01f);

    // The song as the agent sees it.
    auto song = s.result ("get_song");
    CHECK_EQ (song["tracks"].size(), (size_t) 1);
    CHECK_EQ (song["tracks"][0]["clips"][0]["notes"].get<int>(), 2);
    CHECK (s.c.message.contains ("AI agent"));

    // Every change an undo step of its own; undo takes back the last (the Sampler), then the one before.
    CHECK (s.c.history.size() >= before + 6);
    s.result ("undo");
    CHECK_EQ (s.c.project.track (t)->devices.size(), (size_t) 1);
    s.result ("undo");
    CHECK (! s.c.project.track (t)->mute);

    // A script, for anything else; what it prints comes back.
    auto printed = s.result ("run_script", { { "code", "for _, t in ipairs(kalibre.tracks()) do print(t.name) end" } });
    CHECK (printed.get<std::string>().find ("Bass") != std::string::npos);

    // Mistakes come back as the tool's errors, with what to do.
    auto bad = s.call ("create_midi_clip", { { "track", "Nope" }, { "start_beat", 0 }, { "length_beats", 4 } });
    CHECK (bad["isError"].get<bool>());
    bad = s.call ("add_plugin", { { "track", t }, { "plugin", "No Such Plug-in" } });
    CHECK (bad["isError"].get<bool>());
    s.result ("delete_track", { { "track", "Bass" } });
    CHECK (s.c.project.tracks.empty());
}

namespace
{
// A raw HTTP request to Kalibre on this Mac; the whole reply.
std::string rawRequest (int port, const std::string& request)
{
    StreamingSocket sock;
    if (! sock.connect ("127.0.0.1", port, 2000)) return {};
    sock.write (request.data(), (int) request.size());
    std::string in;
    char buf[4096];
    while (sock.waitUntilReady (true, 3000) > 0)
    {
        const int n = sock.read (buf, sizeof buf, false);
        if (n <= 0) break;
        in.append (buf, (size_t) n);
    }
    return in;
}

// Runs `f` on its own thread while the message loop runs (the tools run there).
template <typename F> void whilePumping (F f)
{
    std::atomic<bool> done { false };
    std::thread t ([&] { f(); done = true; });
    while (! done) MessageManager::getInstance()->runDispatchLoopUntil (10);
    t.join();
}
} // namespace

TEST ("agents: a request Kalibre can't get to in time is answered so, and doesn't hold up the next (UAT 23 November)")
{
    // The UAT: one request waited for a message thread busy painting, and
    // every later one waited behind it, for ever.
    Studio s;
    McpServer server (s.c);
    server.answerWithinSeconds = 0.5;
    const int port = 47392;
    server.configure (true, port);
    std::string reply, error;
    // The message thread is this one, and it isn't dispatching: nothing runs.
    CHECK (postToKalibre (port, R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_song","arguments":{}}})", reply, error));
    const auto busy = json::parse (reply, nullptr, false);
    CHECK (! busy.is_discarded() && busy.contains ("error"));
    MessageManager::getInstance()->runDispatchLoopUntil (50);
    bool ok = false;
    whilePumping ([&] { ok = postToKalibre (port, R"({"jsonrpc":"2.0","id":2,"method":"ping"})", reply, error); });
    CHECK (ok);
    const auto next = json::parse (reply, nullptr, false);
    CHECK (! next.is_discarded() && next["id"] == 2 && ! next.contains ("error"));
    server.configure (false, port);
}

TEST ("agents: Kalibre listens on this Mac only when switched on; a web page is refused; the bridge passes messages through (0099)")
{
    Studio s;
    McpServer server (s.c);
    const int port = 47391;
    CHECK (! server.listening());
    server.configure (true, port);
    CHECK (server.listening());
    CHECK (McpServer::discoveryFile().existsAsFile());

    // Streamable HTTP: a POST to /mcp, its reply as JSON.
    std::string reply, error;
    bool ok = false;
    whilePumping ([&] { ok = postToKalibre (port, R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})", reply, error); });
    CHECK (ok);
    const auto init = json::parse (reply, nullptr, false);
    CHECK (! init.is_discarded() && init["result"]["serverInfo"]["name"] == "Kalibre");

    // A notification: accepted, nothing said. A GET: not offered. A web page: refused.
    std::string raw;
    const std::string note = R"({"jsonrpc":"2.0","method":"notifications/initialized"})";
    whilePumping ([&] { raw = rawRequest (port, "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: " + std::to_string (note.size()) + "\r\n\r\n" + note); });
    CHECK (raw.rfind ("HTTP/1.1 202", 0) == 0);
    whilePumping ([&] { raw = rawRequest (port, "GET /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"); });
    CHECK (raw.rfind ("HTTP/1.1 405", 0) == 0);
    whilePumping ([&] { raw = rawRequest (port, "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nOrigin: https://somewhere.example\r\nContent-Length: 2\r\n\r\n{}"); });
    CHECK (raw.rfind ("HTTP/1.1 403", 0) == 0);

    // The bridge: a line in, a line out, through the Kalibre that's open.
    std::istringstream in (R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"create_track","arguments":{"name":"From the Bridge"}}})" "\n"
                           R"({"jsonrpc":"2.0","method":"notifications/initialized"})" "\n");
    std::ostringstream out;
    whilePumping ([&] { runMcpBridge (in, out); });
    const auto lines = StringArray::fromLines (String (out.str()).trim());
    CHECK_EQ (lines.size(), 1);
    CHECK (json::parse (lines[0].toStdString())["id"] == 7);
    CHECK (s.c.project.tracks.size() == 1 && s.c.project.tracks[0].name == "From the Bridge");

    // Switched off: gone, and the bridge says so to the agent instead of hanging.
    server.configure (false, port);
    CHECK (! server.listening());
    CHECK (! McpServer::discoveryFile().existsAsFile());
    std::istringstream again (R"({"jsonrpc":"2.0","id":8,"method":"ping"})" "\n");
    std::ostringstream said;
    runMcpBridge (again, said);
    const auto refused = json::parse (String (said.str()).trim().toStdString());
    CHECK_EQ (refused["id"].get<int>(), 8);
    CHECK (String (refused["error"]["message"].get<std::string>()).contains ("File > Studio > AI Agents"));
}

TEST ("agents: song_version makes version 2 and chooses between them; get_song says which plays (0103)")
{
    Studio s;
    s.result ("create_track", { { "name", "Bass" } });
    CHECK_EQ (s.result ("get_song")["song_versions"].get<int>(), 1);
    s.result ("song_version", { { "version", 2 } });
    CHECK_EQ (s.c.project.songVersions(), 2);
    s.result ("create_track", { { "name", "Lead" } });
    auto song = s.result ("get_song");
    CHECK_EQ (song["song_version"].get<int>(), 2);
    CHECK_EQ (song["tracks"].size(), (size_t) 2);
    s.result ("song_version", { { "version", 1 } });
    CHECK_EQ (s.result ("get_song")["tracks"].size(), (size_t) 1);
    CHECK (s.call ("song_version", { { "version", 3 } })["isError"].get<bool>());
}

TEST ("agents: randomise_plugin sets a device's controls by chance, leaving volume, tuning and pitch (0104)")
{
    Studio s;
    auto track = s.result ("create_track", { { "name", "Bass" }, { "instrument", "Kalibre Synth" } });
    const Id dev = track["devices"][0]["id"];
    const auto steps = s.c.history.size();
    s.result ("randomise_plugin", { { "device", dev } });
    CHECK_EQ (s.c.history.size(), steps + 1);
    CHECK (s.call ("randomise_plugin", { { "device", 999999 } })["isError"].get<bool>());
}

TEST ("agents: a parameter's value reads as it does on the knob: its unit once, no stray decimals (found in the UAT)")
{
    CHECK_EQ (kal::ui::number (502.246, 0), String ("502"));
    CHECK_EQ (kal::ui::number (27.9536, 0), String ("28"));
    CHECK_EQ (kal::ui::number (0.256, 2), String ("0.26"));
    auto synth = makeInstrument (synthRef());
    for (auto* p : synth->getParameters())
        if (p->getName (32) == "Cutoff")
        {
            auto* ranged = dynamic_cast<RangedAudioParameter*> (p);
            const auto text = p->getText (ranged->convertTo0to1 (502.246f), 32);
            CHECK_EQ (text, String ("502 Hz"));
        }
    auto trom = makeInstrument (tromboleseRef());
    for (auto* p : trom->getParameters())
    {
        const auto text = readableValue (*p);
        CHECK (! text.contains ("Pa  Pa") && ! text.contains ("s  s") && ! text.contains ("dB  dB"));
        if (p->getName (32) == "Breath") CHECK_EQ (text, String ("4200 Pa"));
    }
}
