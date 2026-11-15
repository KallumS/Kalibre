# 0005. Folders sum their tracks, hold plug-ins and nest

## Context

Grouping tracks should also group their sound.

## Decision

A folder is a track: its children sum into it, it has devices, a fader and sends of its own, and it can sit inside another folder to any depth. A folder's tracks always follow it in the list (`edit::moveTrack` keeps the block together).

## Consequences

Muting or soloing a folder mutes or solos what's inside it. A folder holds no clips.
