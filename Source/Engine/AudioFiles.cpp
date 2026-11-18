#include "AudioFiles.h"

namespace kal::audiofile
{
using namespace juce;

std::unique_ptr<AudioFormatWriter> floatWav (const File& f, double rate, int channels)
{
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    std::unique_ptr<OutputStream> stream (f.createOutputStream().release());
    if (! stream) return nullptr;
    WavAudioFormat wav;
    return wav.createWriterFor (stream, AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (jmax (1, channels))
                                            .withBitsPerSample (32)
                                            .withSampleFormat (AudioFormatWriterOptions::SampleFormat::floatingPoint));
}

bool isFloatWav (const File& f)
{
    if (! f.hasFileExtension ("wav;wave")) return false;
    WavAudioFormat wav;
    std::unique_ptr<AudioFormatReader> r (wav.createReaderFor (f.createInputStream().release(), true));
    return r && r->bitsPerSample == 32 && r->usesFloatingPointData;
}

bool convertToFloatWav (const File& source, const File& destination)
{
    AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (source));
    if (! r) return false;
    const auto part = destination.getSiblingFile (destination.getFileName() + ".part");
    {
        auto w = floatWav (part, r->sampleRate, (int) r->numChannels);
        if (! w || ! w->writeFromAudioReader (*r, 0, r->lengthInSamples)) { part.deleteFile(); return false; }
    }
    destination.deleteFile();
    return part.moveFileTo (destination);
}

} // namespace kal::audiofile
