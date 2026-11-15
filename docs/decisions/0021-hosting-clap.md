# 0021. Hosting CLAP ourselves

## Context

JUCE 9 hosts AU and VST3, not CLAP.

## Decision

`Engine/ClapFormat.*` wraps a CLAP plug-in as a juce::AudioPluginInstance using the CLAP SDK's headers (MIT): audio, notes and MIDI, parameters, state, latency, transport, timers, and its window embedded (Cocoa on the Mac, X11 on Linux).

## Consequences

Note expressions and surround layouts aren't wired yet.
