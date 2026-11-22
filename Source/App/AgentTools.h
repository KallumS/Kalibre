/*
    AgentTools - what an AI agent can do in Kalibre through MCP (decision
    0099): look at the song; make, change and delete tracks; add plug-ins and
    set their parameters; make clips and write notes; reverse, invert,
    quantise, transpose and split clips; bring audio in; play, stop and
    locate; set the tempo and the loop; undo; export; and run a Lua script
    (docs/SCRIPTING.md) for anything else.

    Every tool runs on the message thread, as a click does. Each change is
    one undo step, labelled "Agent: ...", and said in the message line, so
    the musician sees what was done and can take it back.

    Positions are in beats (quarter notes) from the song's start; a note's
    start is in beats from its clip's start. Tracks are named by id or by
    name; clips and devices by id (get_song lists them).
*/

#pragma once

#include "Controller.h"
#include "Mcp.h"

namespace kal
{
// A parameter's value as it reads, its unit once (0099).
juce::String readableValue (juce::AudioProcessorParameter&);


// Every tool, for this controller.
void addAgentTools (mcp::Server&, Controller&);

// The server Kalibre answers agents with: its name, version, instructions and tools.
std::unique_ptr<mcp::Server> makeAgentServer (Controller&);

} // namespace kal
