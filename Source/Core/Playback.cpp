#include "Playback.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kal::playback
{

namespace
{
std::uint8_t b7 (int v) { return static_cast<std::uint8_t> (std::clamp (v, 0, 127)); }

const Articulation* articulationOf (const Project& p, const Track& t, const Note& n)
{
    if (n.articulation < 0) return nullptr;
    const auto* map = p.articulationMap (t.articulationMap);
    if (! map || n.articulation >= static_cast<int> (map->articulations.size())) return nullptr;
    return &map->articulations[static_cast<size_t> (n.articulation)];
}

MidiMessage message (double seconds, int order, int status, int d1, int d2, std::uint8_t size = 3)
{
    MidiMessage m;
    m.seconds = seconds;
    m.order = order;
    m.bytes[0] = static_cast<std::uint8_t> (status);
    m.bytes[1] = b7 (d1);
    m.bytes[2] = b7 (d2);
    m.size = size;
    return m;
}
} // namespace

double noteDelaySeconds (const Project& p, const Track& t, const Note& n)
{
    double ms = t.delayMs;
    if (const auto* a = articulationOf (p, t, n)) ms += a->delayMs;
    return ms / 1000.0;
}

CompiledTrack compile (const Project& p, const Track& t)
{
    CompiledTrack out;
    out.track = t.id;
    if (! t.holdsClips()) return out;
    const double trackDelay = t.delayMs / 1000.0;
    const auto& tempo = p.tempo;

    for (const auto& c : t.clips())
    {
        if (c.muted) continue;
        if (c.isMidi())
        {
            const Tick content = c.loopLength > 0 ? c.loopLength : std::numeric_limits<Tick>::max() / 4;
            for (Tick base = c.start - c.offset; base < c.end(); base += content)
            {
                for (const auto& n : c.midi.notes)
                {
                    if (c.loopLength > 0 && n.start >= c.loopLength) continue;
                    Tick a = base + n.start, b = a + n.length;
                    if (a < c.start || a >= c.end()) continue;
                    b = std::min (b, c.end());
                    if (c.loopLength > 0) b = std::min (b, base + c.loopLength);
                    if (b <= a) continue;

                    const auto* art = articulationOf (p, t, n);
                    const double delay = noteDelaySeconds (p, t, n);
                    const double on = tempo.secondsAt (a) + delay;
                    const double off = tempo.secondsAt (b) + delay;
                    int channel = n.channel & 15;
                    if (art)
                    {
                        if (art->channel >= 0) channel = art->channel & 15;
                        // What switches the articulation goes a moment before the note.
                        const double pre = on - 0.001;
                        if (art->program >= 0) out.midi.push_back (message (pre, 1, 0xC0 | channel, art->program, 0, 2));
                        if (art->cc >= 0) out.midi.push_back (message (pre, 1, 0xB0 | channel, art->cc, art->ccValue));
                        if (art->keyswitch >= 0)
                        {
                            out.midi.push_back (message (pre, 1, 0x90 | channel, art->keyswitch, art->keyswitchVelocity));
                            out.midi.push_back (message (pre + 0.0005, 0, 0x80 | channel, art->keyswitch, 0));
                        }
                    }
                    out.midi.push_back (message (on, 2, 0x90 | channel, n.pitch, std::max (1, n.velocity)));
                    out.midi.push_back (message (off, 0, 0x80 | channel, n.pitch, 0));
                }
                for (const auto& e : c.midi.events)
                {
                    if (c.loopLength > 0 && e.tick >= c.loopLength) continue;
                    const Tick a = base + e.tick;
                    if (a < c.start || a >= c.end()) continue;
                    const double s = tempo.secondsAt (a) + trackDelay;
                    const int ch = e.channel & 15;
                    switch (e.type)
                    {
                        case EventType::Controller:      out.midi.push_back (message (s, 1, 0xB0 | ch, e.number, e.value)); break;
                        case EventType::PitchBend:       out.midi.push_back (message (s, 1, 0xE0 | ch, e.value & 127, (e.value >> 7) & 127)); break;
                        case EventType::ChannelPressure: out.midi.push_back (message (s, 1, 0xD0 | ch, e.value, 0, 2)); break;
                        case EventType::PolyPressure:    out.midi.push_back (message (s, 1, 0xA0 | ch, e.number, e.value)); break;
                        case EventType::ProgramChange:   out.midi.push_back (message (s, 1, 0xC0 | ch, e.value, 0, 2)); break;
                    }
                }
                if (c.loopLength <= 0) break;
            }
        }
        else
        {
            AudioRegion r;
            r.clip = c.id;
            r.startSeconds = tempo.secondsAt (c.start) + trackDelay;
            r.endSeconds = tempo.secondsAt (c.end()) + trackDelay;
            // A plain clip reads the file itself; a warped, reversed or transposed one
            // reads its own render, which starts where the clip's content starts.
            const bool plain = c.audio.warp == WarpMode::Off && ! c.audio.reversed && std::abs (c.audio.transpose) < 1e-6;
            r.sourceSeconds = plain
                                ? c.audio.sourceStart + (tempo.secondsAt (c.start) - tempo.secondsAt (c.start - c.offset))
                                : tempo.secondsAt (c.start) - tempo.secondsAt (c.start - c.offset);
            r.gain = std::pow (10.0, c.audio.gainDb / 20.0);
            r.fadeIn = c.audio.fadeIn;
            r.fadeOut = c.audio.fadeOut;
            r.file = c.audio.renderedFile.empty() ? c.audio.file : c.audio.renderedFile;
            out.audio.push_back (r);
        }
    }
    std::stable_sort (out.midi.begin(), out.midi.end(), [] (const MidiMessage& a, const MidiMessage& b)
    {
        return a.seconds != b.seconds ? a.seconds < b.seconds : a.order < b.order;
    });
    std::stable_sort (out.audio.begin(), out.audio.end(), [] (const AudioRegion& a, const AudioRegion& b)
    {
        return a.startSeconds < b.startSeconds;
    });
    // Where audio clips overlap, they crossfade over the overlap (decision 0030).
    for (size_t i = 0; i + 1 < out.audio.size(); ++i)
        for (size_t j = i + 1; j < out.audio.size() && out.audio[j].startSeconds < out.audio[i].endSeconds; ++j)
        {
            const double overlap = std::min (out.audio[i].endSeconds, out.audio[j].endSeconds) - out.audio[j].startSeconds;
            if (overlap <= 0) continue;
            out.audio[i].fadeOut = std::max (out.audio[i].fadeOut, out.audio[i].endSeconds - out.audio[j].startSeconds);
            out.audio[j].fadeIn = std::max (out.audio[j].fadeIn, overlap);
        }
    return out;
}

std::vector<CompiledTrack> compileAll (const Project& p)
{
    std::vector<CompiledTrack> out;
    for (auto& t : p.tracks) out.push_back (compile (p, t));
    return out;
}

size_t firstAtOrAfter (const std::vector<MidiMessage>& list, double seconds)
{
    auto it = std::lower_bound (list.begin(), list.end(), seconds,
                                [] (const MidiMessage& m, double s) { return m.seconds < s; });
    return static_cast<size_t> (it - list.begin());
}

} // namespace kal::playback
