# AI agents in Kalibre (MCP)

Kalibre speaks the [Model Context Protocol](https://modelcontextprotocol.io)
(decision 0099), so an AI agent can work in the song. How to switch it on and
connect Claude: the User Guide, [AI agents (MCP)](USER-GUIDE.md#ai-agents-mcp).

## How it's reached

- **Streamable HTTP**, on this Mac only: `POST http://127.0.0.1:47100/mcp`
  (the port is the Studio tab's), a JSON-RPC message in, its reply as JSON.
  A notification gets `202 Accepted`; `GET` gets `405` (no event stream); a
  request whose `Origin` isn't this Mac's gets `403`.
- **stdio**: `Kalibre --mcp` (the app's own executable,
  `/Applications/Kalibre.app/Contents/MacOS/Kalibre`) reads a message a line
  from stdin, passes it to the Kalibre that's open, and writes its reply as a
  line to stdout. When Kalibre isn't open (or agents aren't let in) each
  request gets an error saying so. The port is read from `MCP.json` in
  Kalibre's folder (Application Support), written while Kalibre listens.
- Protocol versions 2025-06-18, 2025-03-26 and 2024-11-05. Tools only (no
  resources or prompts).

## Conventions

- **Positions are in beats** (quarter notes) from the song's start: bar 2 of
  4/4 starts at beat 4. A note's `start_beat` is from its clip's start.
- **Tracks** by id or by name (any case; "master" is the master); **clips and
  devices** by id. `get_song` lists them all; call it first.
- **Pitches** are MIDI numbers: 60 is middle C (C3 in Kalibre's piano roll).
- Every change is **one undo step** ("Agent: …") and said in the message line.
  A failure comes back as the tool's error (`isError`), saying what to do.

## The tools

| Tool | Does |
| --- | --- |
| `get_song` | Tempo, time signature, loop, cursor, playing; every track (id, name, kind, volume, pan, mute, solo, devices with ids, clips with ids and positions) and the master. |
| `list_plugins` | What can be added: the scanned plug-ins and Kalibre's own devices, with maker, format and kind. `search` narrows it. |
| `create_track` | A track, folder or return, after the chosen one; optionally with an `instrument` by name. |
| `update_track` | Name, `volume_db` (-60 to +6), `pan` (-1 to 1), `mute`, `solo`, `armed`. |
| `delete_track` | A track, with its clips and plug-ins. |
| `add_plugin` | A plug-in or Kalibre device onto a track by name; `position` among its kind. |
| `remove_plugin` | A device off its track. |
| `bypass_plugin` | Bypass a device, or bring it back. |
| `get_plugin_parameters` | A device's parameters: index, name, value 0-1, what it reads as. `search` narrows it. |
| `set_plugin_parameter` | A parameter (index or name) to a value 0-1. Like turning the knob: not an undo step of its own. |
| `create_midi_clip` | An empty MIDI clip: `start_beat`, `length_beats`, `name`. |
| `get_clip` | A clip, and a MIDI clip's notes (id, pitch, start, length, velocity). |
| `add_notes` | Notes into a MIDI clip: `pitch`, `start_beat`, `length_beats`, `velocity`. |
| `remove_notes` | Notes by id, or `all`. |
| `edit_clip` | Move (`start_beat`, `track`), trim (`length_beats`), rename, mute. |
| `delete_clip`, `duplicate_clip` | |
| `clip_action` | `reverse`, `invert`, `split_at_transients` (audio), `quantise` (`grid_beats`), `transpose` (`semitones`), `split` (`beat`). |
| `import_audio` | An audio or MIDI file onto a track at a beat; `split_at_transients` for a clip a hit. |
| `transport` | `play`, `stop`, `record`, `go_to_start`, `locate` (`beat`). |
| `set_tempo` | BPM, for the whole song or `from_beat` on. |
| `set_loop` | The loop range in beats, on or off. |
| `undo`, `redo` | |
| `randomise_plugin` | A device's controls by chance, as its Random button (0104): volume, tuning and pitch left alone. One undo step. |
| `song_version` | Plays song version 1 or 2 (making 2, a copy of 1, if there isn't one); one undo step (0103). `get_song` has `song_version` and `song_versions`. |
| `export_mix` | The mix (or `every_track`) to a file; `record_tail`, `second_pass` as Export the Mix's. |
| `run_script` | Lua against the song, for anything else ([SCRIPTING.md](SCRIPTING.md)); what it prints comes back. |
