# 0018. The X/Y morph device

## Context

The user's VST-Morpher, built in.

## Decision

An instrument or effect with four corners, each a plug-in or a sample, mixed by a pad's bilinear weights (VST-Morpher's arithmetic). Sample corners play the track's notes pitched from middle C. Plug-in corners all run and are lined up with the slowest. X and Y are parameters, so they automate and modulate.

## Consequences

A corner's plug-in window is shown beside the pad.
