/*
    Mcp - the Model Context Protocol, so AI agents can work in Kalibre
    (decision 0099). JSON-RPC 2.0: `initialize`, `ping`, `tools/list` and
    `tools/call`. The tools themselves are the App's (AgentTools), each a
    name, what it does, its arguments as a JSON Schema, and a function.

    A message in, its reply out (none for a notification). The transport
    (HTTP on this Mac, or stdin and stdout through `Kalibre --mcp`) is the
    App's too. No JUCE in here.
*/

#pragma once

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace kal::mcp
{

using json = nlohmann::json;

// What a tool hands back: text for the agent; `error` when it couldn't do it.
struct Reply
{
    std::string text;
    bool error = false;
    static Reply ok (std::string t) { return { std::move (t), false }; }
    static Reply fail (std::string t) { return { std::move (t), true }; }
};

struct Tool
{
    std::string name, description;
    json inputSchema;                                   // {"type": "object", "properties": ...}
    std::function<Reply (const json& arguments)> run;
    bool readOnly = false;                              // only looks (an annotation for the agent)
};

// The protocol versions this server speaks, newest first.
const std::vector<std::string>& protocolVersions();

class Server
{
public:
    Server (std::string name, std::string version, std::string instructions = {});
    void add (Tool);
    const std::vector<Tool>& tools() const { return list; }

    // One JSON-RPC message (or a batch) in; the reply, or "" when none is due
    // (a notification, or a batch of them).
    std::string handle (const std::string& message);
    json handleJson (const json& message);

private:
    std::string serverName, serverVersion, guide;
    std::vector<Tool> list;
    json one (const json& message);
};

// A JSON-RPC error reply.
json errorReply (const json& id, int code, const std::string& message);

// Builders for tools' input schemas.
json object (json properties, std::vector<std::string> required = {});
json number (const std::string& description);
json integer (const std::string& description);
json text (const std::string& description);
json flag (const std::string& description);
json oneOf (const std::string& description, std::vector<std::string> values);
json idOrName (const std::string& description);   // a number (the id) or a string (the name)

} // namespace kal::mcp
