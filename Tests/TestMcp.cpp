// The Model Context Protocol's side of Kalibre (decision 0099): JSON-RPC in, replies out.
#include "Check.h"
#include "Mcp.h"

using namespace kal;
using json = nlohmann::json;

namespace
{
mcp::Server server()
{
    mcp::Server s ("Kalibre", "1.0", "A DAW.");
    mcp::Tool add;
    add.name = "add";
    add.description = "Adds two numbers.";
    add.inputSchema = mcp::object ({ { "a", mcp::number ("one") }, { "b", mcp::number ("the other") } }, { "a", "b" });
    add.run = [] (const json& a)
    {
        if (! a.contains ("a") || ! a.contains ("b")) return mcp::Reply::fail ("Two numbers, please.");
        return mcp::Reply::ok (std::to_string (a["a"].get<int>() + a["b"].get<int>()));
    };
    add.readOnly = true;
    s.add (add);
    mcp::Tool boom;
    boom.name = "boom";
    boom.description = "Throws.";
    boom.inputSchema = mcp::object ({});
    boom.run = [] (const json&) -> mcp::Reply { throw std::runtime_error ("no track 9"); };
    s.add (boom);
    return s;
}

json call (mcp::Server& s, const json& m) { return json::parse (s.handle (m.dump())); }
} // namespace

TEST ("mcp: initialize answers with the client's protocol version when it's known, its tools and instructions")
{
    auto s = server();
    auto r = call (s, { { "jsonrpc", "2.0" }, { "id", 1 }, { "method", "initialize" },
                        { "params", { { "protocolVersion", "2025-03-26" }, { "capabilities", json::object() },
                                      { "clientInfo", { { "name", "test" }, { "version", "1" } } } } } });
    CHECK_EQ (r["id"].get<int>(), 1);
    CHECK_EQ (r["result"]["protocolVersion"].get<std::string>(), std::string ("2025-03-26"));
    CHECK (r["result"]["capabilities"].contains ("tools"));
    CHECK_EQ (r["result"]["serverInfo"]["name"].get<std::string>(), std::string ("Kalibre"));
    CHECK_EQ (r["result"]["instructions"].get<std::string>(), std::string ("A DAW."));
    // A version it doesn't know: its own newest.
    r = call (s, { { "jsonrpc", "2.0" }, { "id", 2 }, { "method", "initialize" }, { "params", { { "protocolVersion", "1999-01-01" } } } });
    CHECK_EQ (r["result"]["protocolVersion"].get<std::string>(), mcp::protocolVersions().front());
    // A notification has no reply.
    CHECK (s.handle (json ({ { "jsonrpc", "2.0" }, { "method", "notifications/initialized" } }).dump()).empty());
}

TEST ("mcp: tools are listed with their schemas, and called; a failure is the tool's, an unknown name the protocol's")
{
    auto s = server();
    auto r = call (s, { { "jsonrpc", "2.0" }, { "id", "a" }, { "method", "tools/list" } });
    CHECK_EQ (r["result"]["tools"].size(), (size_t) 2);
    CHECK_EQ (r["result"]["tools"][0]["name"].get<std::string>(), std::string ("add"));
    CHECK_EQ (r["result"]["tools"][0]["inputSchema"]["required"].size(), (size_t) 2);
    CHECK (r["result"]["tools"][0]["annotations"]["readOnlyHint"].get<bool>());

    r = call (s, { { "jsonrpc", "2.0" }, { "id", 3 }, { "method", "tools/call" }, { "params", { { "name", "add" }, { "arguments", { { "a", 2 }, { "b", 3 } } } } } });
    CHECK_EQ (r["result"]["content"][0]["text"].get<std::string>(), std::string ("5"));
    CHECK (! r["result"]["isError"].get<bool>());

    r = call (s, { { "jsonrpc", "2.0" }, { "id", 4 }, { "method", "tools/call" }, { "params", { { "name", "add" }, { "arguments", { { "a", 2 } } } } } });
    CHECK (r["result"]["isError"].get<bool>());
    r = call (s, { { "jsonrpc", "2.0" }, { "id", 5 }, { "method", "tools/call" }, { "params", { { "name", "boom" } } } });
    CHECK (r["result"]["isError"].get<bool>());
    CHECK (r["result"]["content"][0]["text"].get<std::string>().find ("no track 9") != std::string::npos);

    r = call (s, { { "jsonrpc", "2.0" }, { "id", 6 }, { "method", "tools/call" }, { "params", { { "name", "nope" } } } });
    CHECK_EQ (r["error"]["code"].get<int>(), -32602);
    r = call (s, { { "jsonrpc", "2.0" }, { "id", 7 }, { "method", "sing" } });
    CHECK_EQ (r["error"]["code"].get<int>(), -32601);
    CHECK_EQ (json::parse (s.handle ("{not json"))["error"]["code"].get<int>(), -32700);
    // A batch: each answered; its notifications not.
    r = call (s, json::array ({ { { "jsonrpc", "2.0" }, { "id", 8 }, { "method", "ping" } },
                                { { "jsonrpc", "2.0" }, { "method", "notifications/initialized" } } }));
    CHECK (r.is_array() && r.size() == 1);
}
