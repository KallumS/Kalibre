#include "Mcp.h"

namespace kal::mcp
{

const std::vector<std::string>& protocolVersions()
{
    static const std::vector<std::string> v { "2025-06-18", "2025-03-26", "2024-11-05" };
    return v;
}

Server::Server (std::string name, std::string version, std::string instructions)
    : serverName (std::move (name)), serverVersion (std::move (version)), guide (std::move (instructions)) {}

void Server::add (Tool t) { list.push_back (std::move (t)); }

json errorReply (const json& id, int code, const std::string& message)
{
    return { { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", code }, { "message", message } } } };
}

std::string Server::handle (const std::string& message)
{
    const auto j = json::parse (message, nullptr, false);
    if (j.is_discarded()) return errorReply (nullptr, -32700, "That isn't JSON.").dump();
    const auto r = handleJson (j);
    return r.is_null() ? std::string() : r.dump();
}

json Server::handleJson (const json& message)
{
    if (message.is_array())
    {
        if (message.empty()) return errorReply (nullptr, -32600, "An empty batch.");
        json out = json::array();
        for (auto& m : message) if (auto r = one (m); ! r.is_null()) out.push_back (r);
        return out.empty() ? json() : out;
    }
    return one (message);
}

json Server::one (const json& m)
{
    if (! m.is_object() || ! m.contains ("method") || ! m["method"].is_string())
    {
        // A reply to something we asked (we never ask): nothing to say.
        if (m.is_object() && (m.contains ("result") || m.contains ("error"))) return {};
        return errorReply (m.is_object() && m.contains ("id") ? m["id"] : json(), -32600, "Not a JSON-RPC request.");
    }
    const std::string method = m["method"];
    const bool notification = ! m.contains ("id");
    const json id = notification ? json() : m["id"];
    const json params = m.contains ("params") && m["params"].is_object() ? m["params"] : json::object();
    auto result = [&id] (json r) { return json { { "jsonrpc", "2.0" }, { "id", id }, { "result", std::move (r) } }; };

    if (notification) return {};   // notifications/initialized, cancelled...: nothing to answer
    if (method == "initialize")
    {
        // The client's version when we speak it, else our newest.
        std::string version = protocolVersions().front();
        if (params.contains ("protocolVersion") && params["protocolVersion"].is_string())
            for (auto& v : protocolVersions()) if (v == params["protocolVersion"]) version = v;
        json r = { { "protocolVersion", version },
                   { "capabilities", { { "tools", { { "listChanged", false } } } } },
                   { "serverInfo", { { "name", serverName }, { "version", serverVersion } } } };
        if (! guide.empty()) r["instructions"] = guide;
        return result (r);
    }
    if (method == "ping") return result (json::object());
    if (method == "tools/list")
    {
        json tools = json::array();
        for (auto& t : list)
        {
            json d = { { "name", t.name }, { "description", t.description }, { "inputSchema", t.inputSchema } };
            d["annotations"] = { { "readOnlyHint", t.readOnly } };
            tools.push_back (d);
        }
        return result ({ { "tools", tools } });
    }
    if (method == "tools/call")
    {
        if (! params.contains ("name") || ! params["name"].is_string()) return errorReply (id, -32602, "tools/call needs a tool's name.");
        const std::string name = params["name"];
        const json args = params.contains ("arguments") && params["arguments"].is_object() ? params["arguments"] : json::object();
        for (auto& t : list)
            if (t.name == name)
            {
                Reply r;
                try { r = t.run (args); }
                catch (const std::exception& e) { r = Reply::fail (std::string ("It couldn't be done: ") + e.what()); }
                return result ({ { "content", json::array ({ { { "type", "text" }, { "text", r.text } } }) }, { "isError", r.error } });
            }
        return errorReply (id, -32602, "No tool is called " + name + ".");
    }
    // Asked for what this server doesn't offer (resources, prompts...).
    if (method == "resources/list") return result ({ { "resources", json::array() } });
    if (method == "prompts/list") return result ({ { "prompts", json::array() } });
    return errorReply (id, -32601, "Kalibre doesn't know " + method + ".");
}

json object (json properties, std::vector<std::string> required)
{
    json o = { { "type", "object" }, { "properties", properties.is_null() ? json::object() : properties } };
    if (! required.empty()) o["required"] = required;
    return o;
}
json number (const std::string& d) { return { { "type", "number" }, { "description", d } }; }
json integer (const std::string& d) { return { { "type", "integer" }, { "description", d } }; }
json text (const std::string& d) { return { { "type", "string" }, { "description", d } }; }
json flag (const std::string& d) { return { { "type", "boolean" }, { "description", d } }; }
json oneOf (const std::string& d, std::vector<std::string> values) { return { { "type", "string" }, { "enum", values }, { "description", d } }; }
json idOrName (const std::string& d) { return { { "type", json::array ({ "integer", "string" }) }, { "description", d } }; }

} // namespace kal::mcp
