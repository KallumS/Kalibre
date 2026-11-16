#include "Mp3Writer.h"

#include "ProjectTools.h"

extern "C"
{
#include <layer3.h>
}

namespace kal
{
using namespace juce;

bool writeMp3 (const File& f, const AudioBuffer<float>& source, double rate, const Metadata& meta, int kbps, String& error)
{
    // MPEG-1 layer III runs at 32, 44.1 or 48 kHz.
    AudioBuffer<float> audio;
    double outRate = rate;
    if (rate != 44100.0 && rate != 48000.0 && rate != 32000.0)
    {
        outRate = 48000.0;
        const double ratio = rate / outRate;
        const int frames = (int) std::ceil (source.getNumSamples() / ratio);
        audio.setSize (2, frames);
        for (int ch = 0; ch < 2; ++ch)
        {
            LagrangeInterpolator resampler;
            resampler.process (ratio, source.getReadPointer (jmin (ch, source.getNumChannels() - 1)), audio.getWritePointer (ch), frames);
        }
    }
    else
    {
        audio.setSize (2, source.getNumSamples());
        for (int ch = 0; ch < 2; ++ch) audio.copyFrom (ch, 0, source, jmin (ch, source.getNumChannels() - 1), 0, source.getNumSamples());
    }

    shine_config_t config {};
    shine_set_config_mpeg_defaults (&config.mpeg);
    config.wave.channels = PCM_STEREO;
    config.wave.samplerate = (int) outRate;
    config.mpeg.mode = JOINT_STEREO;
    config.mpeg.bitr = kbps;
    if (shine_check_config (config.wave.samplerate, config.mpeg.bitr) < 0) { error = "That bit rate can't be used at this sample rate."; return false; }
    shine_t encoder = shine_initialise (&config);
    if (! encoder) { error = "The MP3 encoder couldn't start."; return false; }

    f.deleteFile();
    FileOutputStream out (f);
    if (! out.openedOk()) { shine_close (encoder); error = "The file couldn't be written."; return false; }
    const auto tag = projecttools::id3v2 (meta);
    if (! tag.empty()) out.write (tag.data(), tag.size());

    const int perPass = shine_samples_per_pass (encoder);
    std::vector<int16_t> left ((size_t) perPass), right ((size_t) perPass);
    for (int pos = 0; pos < audio.getNumSamples(); pos += perPass)
    {
        for (int i = 0; i < perPass; ++i)
        {
            const int at = pos + i;
            auto sample = [&] (int ch) { return at < audio.getNumSamples() ? (int16_t) roundToInt (jlimit (-1.0f, 1.0f, audio.getSample (ch, at)) * 32767.0f) : (int16_t) 0; };
            left[(size_t) i] = sample (0);
            right[(size_t) i] = sample (1);
        }
        int16_t* channels[2] { left.data(), right.data() };
        int written = 0;
        if (auto* data = shine_encode_buffer (encoder, channels, &written); data && written > 0) out.write (data, (size_t) written);
    }
    int written = 0;
    if (auto* data = shine_flush (encoder, &written); data && written > 0) out.write (data, (size_t) written);
    shine_close (encoder);
    out.flush();
    return true;
}

} // namespace kal
