#include "McpServer.h"
#include "AgentTools.h"

#include <iostream>

namespace kal
{
using namespace juce;

namespace
{
// Reads from a socket until `until` holds, the peer stops or a few seconds pass.
bool readSome (StreamingSocket& s, std::string& into, const std::function<bool()>& until)
{
    char buf[8192];
    const auto deadline = Time::getMillisecondCounter() + 10000;
    while (! until())
    {
        if (Time::getMillisecondCounter() > deadline) return false;
        const int ready = s.waitUntilReady (true, 200);
        if (ready < 0) return false;
        if (ready == 0) continue;
        const int n = s.read (buf, sizeof buf, false);
        if (n <= 0) return until();
        into.append (buf, (size_t) n);
        if (into.size() > 32 * 1024 * 1024) return false;   // nothing an agent sends is that big
    }
    return true;
}

bool writeAll (StreamingSocket& s, const std::string& text)
{
    size_t done = 0;
    while (done < text.size())
    {
        const int n = s.write (text.data() + done, (int) (text.size() - done));
        if (n <= 0) return false;
        done += (size_t) n;
    }
    return true;
}

std::string response (int code, const char* status, const std::string& body, const char* type = "application/json")
{
    std::string r = "HTTP/1.1 " + std::to_string (code) + " " + status + "\r\n";
    if (! body.empty()) r += std::string ("Content-Type: ") + type + "\r\n";
    r += "Content-Length: " + std::to_string (body.size()) + "\r\nConnection: close\r\n";
    if (code == 405) r += "Allow: POST\r\n";
    r += "\r\n" + body;
    return r;
}

// A header's value (any case), or "".
String header (const String& head, const String& name)
{
    for (auto& line : StringArray::fromLines (head))
        if (line.startsWithIgnoreCase (name + ":")) return line.fromFirstOccurrenceOf (":", false, false).trim();
    return {};
}

// A request from a web page names where it came from; only this Mac's own are let in.
bool localOrigin (const String& origin)
{
    if (origin.isEmpty() || origin == "null") return true;
    const URL u (origin);
    const auto host = u.getDomain();
    return host == "localhost" || host == "127.0.0.1" || host == "[::1]";
}
} // namespace

//==============================================================================
McpServer::McpServer (Controller& ctrl) : Thread ("Kalibre MCP"), c (ctrl), server (makeAgentServer (ctrl)) {}

McpServer::~McpServer() { configure (false, 0); }

File McpServer::discoveryFile()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
          #if JUCE_MAC
               .getChildFile ("Application Support")
          #endif
               .getChildFile ("Kalibre/MCP.json");
}

void McpServer::configure (bool on, int wanted)
{
    if (on && listening() && port == wanted) return;
    // Off first (or moving): the listener is closed, which wakes the thread.
    if (isThreadRunning())
    {
        signalThreadShouldExit();
        listener.close();
        stopThread (4000);
    }
    if (port > 0) discoveryFile().deleteFile();
    port = 0;
    lastError.clear();
    if (! on) return;
    listener.close();
    if (! listener.createListener (wanted, "127.0.0.1"))
    {
        lastError = "Port " + std::to_string (wanted) + " is in use by something else: choose another.";
        return;
    }
    port = wanted;
    discoveryFile().getParentDirectory().createDirectory();
    discoveryFile().replaceWithText (String (mcp::json ({ { "port", port }, { "url", "http://127.0.0.1:" + std::to_string (port) + "/mcp" } }).dump (1)));
    startThread();
}

void McpServer::run()
{
    while (! threadShouldExit())
    {
        std::unique_ptr<StreamingSocket> client (listener.waitForNextConnection());
        if (threadShouldExit()) break;
        if (client) answer (*client);
        else Thread::sleep (20);
    }
}

void McpServer::answer (StreamingSocket& s)
{
    std::string in;
    size_t headEnd = std::string::npos;
    if (! readSome (s, in, [&] { return (headEnd = in.find ("\r\n\r\n")) != std::string::npos; })) return;
    const String head (in.substr (0, headEnd));
    const auto requestLine = StringArray::fromTokens (head.upToFirstOccurrenceOf ("\r\n", false, false), " ", {});
    const String method = requestLine[0], path = requestLine[1].upToFirstOccurrenceOf ("?", false, false);
    if (! localOrigin (header (head, "Origin"))) { writeAll (s, response (403, "Forbidden", "")); return; }
    if (path != "/mcp" && path != "/") { writeAll (s, response (404, "Not Found", "")); return; }
    if (method != "POST") { writeAll (s, response (405, "Method Not Allowed", "")); return; }
    const auto length = (size_t) jmax (0, header (head, "Content-Length").getIntValue());
    const size_t bodyStart = headEnd + 4;
    if (! readSome (s, in, [&] { return in.size() >= bodyStart + length; })) return;
    const auto reply = onMessageThread (in.substr (bodyStart, length));
    // A notification, or a reply to nothing: accepted, nothing to say.
    if (reply.empty()) writeAll (s, response (202, "Accepted", ""));
    else writeAll (s, response (200, "OK", reply));
}

// The tools touch the song: they run where a click would, and this waits for them.
std::string McpServer::onMessageThread (const std::string& body)
{
    if (MessageManager::existsAndIsCurrentThread()) return server->handle (body);
    struct Job { std::string in, out; WaitableEvent done; };
    auto job = std::make_shared<Job>();
    job->in = body;
    MessageManager::callAsync ([this, job]
    {
        job->out = server->handle (job->in);
        job->done.signal();
    });
    // An export can take a while; a few minutes is plenty. Past that the agent
    // is told, and what it asked may still happen (the UAT: one request waited
    // for ever, every later one behind it, decision 0133).
    const auto until = Time::getMillisecondCounterHiRes() + answerWithinSeconds * 1000.0;
    while (! job->done.wait (jmax (1, jmin (200, (int) (answerWithinSeconds * 1000.0)))))
    {
        if (threadShouldExit()) return mcp::errorReply (nullptr, -32000, "Kalibre is closing.").dump();
        if (Time::getMillisecondCounterHiRes() > until)
        {
            const auto request = mcp::json::parse (body, nullptr, false);
            return mcp::errorReply (request.is_object() && request.contains ("id") ? request["id"] : mcp::json(), -32000,
                                    "Kalibre is busy and didn't get to this in time; it may still do it. Call get_song to see, then try again.").dump();
        }
    }
    return job->out;
}

String McpServer::desktopSetup()
{
    const auto exe = File::getSpecialLocation (File::currentExecutableFile).getFullPathName();
    mcp::json j = { { "mcpServers", { { "kalibre", { { "command", exe.toStdString() }, { "args", { bridgeFlag } } } } } } };
    return String (j.dump (2));
}

String McpServer::claudeCodeSetup()
{
    const auto exe = File::getSpecialLocation (File::currentExecutableFile).getFullPathName();
    return "claude mcp add kalibre -- \"" + exe + "\" " + bridgeFlag;
}

//==============================================================================
bool postToKalibre (int port, const std::string& body, std::string& reply, std::string& error)
{
    StreamingSocket s;
    if (port <= 0 || ! s.connect ("127.0.0.1", port, 2000))
    {
        error = "Kalibre isn't open, or doesn't let AI agents in yet: open Kalibre, and switch on AI Agents in File > Studio > AI Agents.";
        return false;
    }
    const std::string request = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string (port)
                              + "\r\nContent-Type: application/json\r\nAccept: application/json, text/event-stream\r\nContent-Length: "
                              + std::to_string (body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    if (! writeAll (s, request)) { error = "Kalibre stopped answering."; return false; }
    std::string in;
    char buf[8192];
    for (;;)
    {
        const int ready = s.waitUntilReady (true, 600000);   // an export can take a while
        if (ready <= 0) break;
        const int n = s.read (buf, sizeof buf, false);
        if (n <= 0) break;
        in.append (buf, (size_t) n);
    }
    const auto headEnd = in.find ("\r\n\r\n");
    if (headEnd == std::string::npos) { error = "Kalibre's reply was cut short."; return false; }
    const String status (in.substr (0, in.find ("\r\n")));
    if (! status.contains (" 200 ") && ! status.contains (" 202 ")) { error = "Kalibre refused: " + status.toStdString(); return false; }
    reply = in.substr (headEnd + 4);
    return true;
}

int runMcpBridge (std::istream& in, std::ostream& out)
{
    std::string line;
    while (std::getline (in, line))
    {
        if (String (line).trim().isEmpty()) continue;
        // Where Kalibre listens, read again each time: it may have been opened since.
        int port = 0;
        const auto found = mcp::json::parse (McpServer::discoveryFile().loadFileAsString().toStdString(), nullptr, false);
        if (found.is_object() && found.contains ("port") && found["port"].is_number_integer()) port = found["port"];
        std::string reply, error;
        if (! postToKalibre (port, line, reply, error))
        {
            // Said to the agent as the protocol's error, for a request; a notification needs no answer.
            const auto msg = mcp::json::parse (line, nullptr, false);
            if (msg.is_object() && msg.contains ("id")) reply = mcp::errorReply (msg["id"], -32000, error).dump();
            else continue;
        }
        if (String (reply).trim().isEmpty()) continue;
        out << String (reply).trim().toStdString() << "\n";
        out.flush();
    }
    return 0;
}

} // namespace kal
