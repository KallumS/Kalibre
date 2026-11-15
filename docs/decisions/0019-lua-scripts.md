# 0019. Scripts in Lua

## Context

The user writes REAPER scripts in Lua.

## Decision

Lua 5.4 is built in. Scripts in ~/Library/Application Support/Kalibre/Scripts appear in the Scripts menu and see the project through `kalibre` (docs/SCRIPTING.md). A run is one undo step, a failing script changes nothing, and a runaway one is stopped after five seconds.

## Consequences

The API grows as it's needed.
