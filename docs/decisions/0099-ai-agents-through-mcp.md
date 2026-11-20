# 0099. AI agents through MCP

## Context

Asked (20 November): MCP support, so external AI agents can do things in
Kalibre: create tracks, add plug-ins, work with clips, and so on.

## Decision

- **A server inside Kalibre, off until switched on.** The Studio tab gets
  **AI Agents (MCP)** (also Kalibre > AI Agents…): **Let AI Agents Control
  Kalibre**, a port (47100), what it's doing, and two buttons that copy the
  setup for Claude Desktop and Claude Code. Kept in Studio.json with the
  other studio settings (`mcpOn`, `mcpPort`).
- **Two ways in, one server.** Streamable HTTP on 127.0.0.1 (`POST /mcp`,
  JSON replies, no event stream: `GET` is 405), for agents that connect to a
  URL; and `Kalibre --mcp`, for agents that start a program and talk over
  stdin and stdout (Claude Desktop, Claude Code). The bridge runs from
  `main` before the app starts, as the scanner's helper does (0047), finds
  the port in MCP.json (written while listening) and passes each line on.
  So there's one implementation of the tools, whichever way an agent comes.
- **Safety.** Listening on 127.0.0.1 only; a request carrying a web page's
  `Origin` is refused (a page can't drive Kalibre from the browser); off by
  default. No token: anything on this Mac that can run a program could run
  `Kalibre --mcp` anyway, and the musician switches it on knowingly.
- **The protocol is Core** (`Core/Mcp`, nlohmann/json, no JUCE): initialize
  (versions 2025-06-18, 2025-03-26, 2024-11-05), ping, tools/list, tools/call,
  batches; a tool's failure is its own `isError`, an unknown method or tool
  the protocol's error. Tested in seconds.
- **The tools are the App's** (`App/AgentTools`), 25 of them (docs/AGENTS.md):
  the song as JSON, tracks, plug-ins (by name, Kalibre's own included) and
  their parameters, MIDI clips and notes, clip actions (reverse, invert,
  split at transients, quantise, transpose, split), import, transport,
  tempo, loop, undo and redo, export (with Record the Tail and the Second
  Pass, 0097), and **run_script**: Lua through the existing API (0019) for
  anything else. Positions in beats; tracks by id or name; clips and devices
  by id.
- **As a click.** Each request's tool runs on the message thread (the server
  thread waits for it). Every change is one `Controller::apply`, labelled
  "Agent: …", and said in the message line with Undo. Setting a plug-in's
  parameter is like turning its knob: not an undo step, as for the musician.
- `Controller::runScriptCode` runs a script's text (the Scripts menu now
  goes through it too).

## Consequences

- Tested: the protocol (versions, tools, errors, batches, notifications);
  an agent making a track with an instrument, a clip, notes, transposing,
  changing the track, adding the Sampler and setting its Root Note, undoing
  each, a script printing back, mistakes as errors; the server listening only
  when on, a notification 202, GET 405, a web page's Origin 403; the bridge
  passing a request through and, with Kalibre off, answering with an error
  that says where to switch it on. Driven end to end: the real app open under
  Xvfb, `Kalibre --mcp` making a track with Kalibre Synth and a clip.
- Not yet tried: Claude Desktop itself on the Mac (the setup it's given is
  the documented `mcpServers` form); a plug-in window open while an agent
  sets its parameters.
- The tools grow as they're asked for; `run_script` covers the rest
  meanwhile.
