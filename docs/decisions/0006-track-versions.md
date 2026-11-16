# 0006. Track versions

## Context

Trying another take or arrangement of a part shouldn't mean another track.

## Decision

A track has versions; each is a complete set of clips. Right-click a track > New Version copies the active one; the version number on the header (v1, v2...) chooses between them. A version's clips stay until that version is deleted. Devices, the mix and automation are shared by every version.

Changed 17 November 2026 (0050): four versions at most, each with its own
plug-ins, chosen from the mixer's 1 to 4 buttons.

## Consequences

Versions are not takes: comping is a later feature.
