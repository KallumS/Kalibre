/*
    Mp3Writer - an export as an MP3 (decision 0041), through Shine.

    320 kb/s joint stereo at 44.1 or 48 kHz (anything else is resampled to
    48 kHz first), with the song's details as an ID3v2 tag in front.
*/

#pragma once

#include "Model.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace kal
{

bool writeMp3 (const juce::File&, const juce::AudioBuffer<float>&, double sampleRate, const Metadata&,
               int kbps, juce::String& error);

} // namespace kal
