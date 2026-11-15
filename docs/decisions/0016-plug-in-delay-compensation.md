# 0016. Plug-in delay compensation by the audio graph

## Context

Plug-ins that look ahead delay their sound; tracks must stay together.

## Decision

The project is a juce::AudioProcessorGraph. Wherever paths meet, the graph delays the quicker ones to match the slowest, for AU, VST3, CLAP and Kalibre's own devices. The playhead shown is the position less the graph's total delay.

## Consequences

Tested by rendering a click through a 300-sample plug-in beside a dry one: one click comes out, not two.
