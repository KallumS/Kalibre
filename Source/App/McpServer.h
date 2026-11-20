/*
    McpServer - Kalibre answering AI agents through the Model Context
    Protocol (decision 0099).

    Off until switched on (the browser's Studio tab: AI Agents). On, Kalibre
    listens on 127.0.0.1 (this Mac only) for MCP's Streamable HTTP: a POST
    to /mcp with a JSON-RPC message, its reply as JSON. A request from a web
    page (an Origin that isn't this Mac's) is refused. Each request's tools
    run on the message thread, as a click does. While listening, the port is
    written to MCP.json in Kalibre's folder, for the bridge to find.

    The bridge: `Kalibre --mcp` is an MCP server on stdin and stdout, as
    Claude Desktop and Claude Code start one; it passes each message to the
    Kalibre that's open, and its reply back.
*/

#pragma once

#include "Controller.h"
#include "Mcp.h"

#include <iosfwd>

namespace kal
{

class McpServer : private juce::Thread
{
public:
    explicit McpServer (Controller&);
    ~McpServer() override;

    // On (or moved to another port) or off, from the studio's settings.
    void configure (bool on, int port);
    bool listening() const { return isThreadRunning() && port > 0; }
    int listeningPort() const { return port; }
    std::string lastError;

    // Where the bridge finds the port: MCP.json in Kalibre's folder.
    static juce::File discoveryFile();
    // The flag that starts the bridge instead of Kalibre.
    static constexpr const char* bridgeFlag = "--mcp";
    // What to give an agent to reach Kalibre: Claude Desktop's settings, and Claude Code's command.
    static juce::String desktopSetup();
    static juce::String claudeCodeSetup();

    mcp::Server& protocol() { return *server; }

private:
    Controller& c;
    std::unique_ptr<mcp::Server> server;
    juce::StreamingSocket listener;
    int port = 0;
    void run() override;
    void answer (juce::StreamingSocket&);
    std::string onMessageThread (const std::string& body);
};

// The bridge: messages a line at a time from `in`, each passed to Kalibre on
// this Mac; replies a line each to `out`. Until `in` ends. 0 when it ended cleanly.
int runMcpBridge (std::istream& in, std::ostream& out);

// One message to the Kalibre listening on `port`; its reply ("" for none). False when it couldn't be reached.
bool postToKalibre (int port, const std::string& body, std::string& reply, std::string& error);

} // namespace kal
