# 0001. Built with JUCE and C++

## Context

Kalibre has to host AU, VST3 and CLAP plug-ins, show their windows inside its own, host ARA plug-ins and run audio in real time on Apple silicon. Swift, Rust, Lua and C++/JUCE were weighed.

## Decision

C++20 with JUCE 9.0.5, fetched by CMake at a fixed tag. Lua 5.4 is built in for scripts (0019). JUCE hosts AU and VST3, has ARA hosting, and a plug-in's window is an ordinary component, so it can sit in the lower zone. Swift would need C++ bridges for VST3, CLAP and ARA and is risky on the audio thread; Rust has no maintained AU or ARA hosting; Lua is too slow for audio. VST-Morpher, Miderator and Noterator are already JUCE, so their code and look carry over.

## Consequences

JUCE is AGPLv3, as Kalibre is. Selling a closed build would need a JUCE licence. CLAP isn't hosted by JUCE, so Kalibre hosts it itself (0021).
