/*
    AudioFiles - the one format Kalibre keeps audio in (decision 0079):
    32-bit floating-point WAV, for everything it writes for itself
    (recordings, captures, freezes, bounces, offline effects, sample edits)
    and for audio brought in (an MP3, a FLAC, a 16-bit WAV is copied into the
    song's Audio folder as one). Nothing is lost to rounding inside Kalibre,
    and a level over 0 dB isn't clipped. Exports are as the user chooses.
*/

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

namespace kal::audiofile
{

// A writer for a 32-bit float WAV (the file is replaced); null if it can't be made.
std::unique_ptr<juce::AudioFormatWriter> floatWav (const juce::File&, double sampleRate, int channels);

// Whether the file is a 32-bit float WAV already.
bool isFloatWav (const juce::File&);

// Any audio Kalibre can read, written as a 32-bit float WAV at its own rate.
bool convertToFloatWav (const juce::File& source, const juce::File& destination);

} // namespace kal::audiofile
