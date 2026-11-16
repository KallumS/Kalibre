#include "Playback.h"

#include "Mpe.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace kal::playback
{

namespace
{
void eraseIfRegion (std::vector<AudioRegion>& regions, double from)
{
    regions.erase (std::remove_if (regions.begin(), regions.end(), [from] (const AudioRegion& r) { return r.startSeconds >= from; }), regions.end());
    for (auto& r : regions)
        if (r.endSeconds > from) { r.endSeconds = from; r.fadeOut = std::min (0.005, from - r.startSeconds); }
}

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

// The clip launcher (decision 0037): from `launch.from` the arrangement is
// cut off and the slot's clip plays over and over.
static void applyLaunch (const Project& p, const Track& t, CompiledTrack& out)
{
    const double from = p.tempo.secondsAt (t.launch.from) + t.delayMs / 1000.0;
    std::vector<MidiMessage> kept;
    std::array<int, 16 * 128> open {};
    for (auto& m : out.midi)
    {
        const int status = m.bytes[0] & 0xF0, key = (m.bytes[0] & 15) * 128 + (m.bytes[1] & 127);
        const bool on = status == 0x90 && m.bytes[2] > 0, off = status == 0x80 || (status == 0x90 && m.bytes[2] == 0);
        if (m.seconds < from)
        {
            kept.push_back (m);
            if (on) ++open[(size_t) key];
            else if (off && open[(size_t) key] > 0) --open[(size_t) key];
        }
        else if (off && open[(size_t) key] > 0)
        {
            // A note still sounding when the slot takes over ends there.
            auto x = m;
            x.seconds = from;
            kept.push_back (x);
            --open[(size_t) key];
        }
    }
    out.midi = std::move (kept);
    eraseIfRegion (out.audio, from);

    if (t.launch.slot < 0 || t.launch.slot >= (int) t.slots.size()) return;
    const Clip& slot = t.slots[(size_t) t.launch.slot];
    const Tick each = slot.loopLength > 0 ? std::max<Tick> (slot.loopLength, slot.length) : slot.length;
    if (each <= 0) return;
    // Thirty minutes of repeats ahead is plenty; launching again recompiles.
    const Tick until = p.tempo.tickAt (p.tempo.secondsAt (t.launch.from) + 1800.0);
    Track loop = t;
    loop.launch = {};
    loop.slots.clear();
    loop.versions = { TrackVersion {} };
    loop.activeVersion = 0;
    for (Tick at = t.launch.from; at < until && loop.clips().size() < 1024; at += each)
    {
        Clip c = slot;
        c.start = at;
        c.muted = false;
        loop.clips().push_back (c);
    }
    const auto more = compile (p, loop);
    out.midi.insert (out.midi.end(), more.midi.begin(), more.midi.end());
    out.audio.insert (out.audio.end(), more.audio.begin(), more.audio.end());
    std::stable_sort (out.midi.begin(), out.midi.end(), [] (const MidiMessage& a, const MidiMessage& b)
    {
        return a.seconds != b.seconds ? a.seconds < b.seconds : a.order < b.order;
    });
    std::stable_sort (out.audio.begin(), out.audio.end(), [] (const AudioRegion& a, const AudioRegion& b) { return a.startSeconds < b.startSeconds; });
}

CompiledTrack compile (const Project& p, const Track& t)
{
    CompiledTrack out;
    out.track = t.id;
    if (! t.holdsClips()) return out;
    if (t.launch.slot >= 0 || t.launch.stopped)
    {
        Track arrangement = t;
        arrangement.launch = {};
        out = compile (p, arrangement);
        applyLaunch (p, t, out);
        return out;
    }
    const double trackDelay = t.delayMs / 1000.0;
    const auto& tempo = p.tempo;
    std::array<double, 16> mpeBusy {};   // when each member channel is free again
    mpeBusy[0] = 1.0e300;                // channel 1 is the zone's master
    if (t.mpe)
    {
        // The MPE zone: channel 1 the master, 15 member channels (MCM, RPN 6).
        for (auto [cc, v] : { std::pair { 101, 0 }, std::pair { 100, 6 }, std::pair { 6, 15 } })
            out.midi.push_back (message (0.0, 0, 0xB0, cc, v));
    }

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
                    if (t.mpe)
                    {
                        // MPE (decision 0042): a channel of its own, 2-16, the one free longest.
                        int best = 1;
                        for (int ch = 1; ch < 16; ++ch)
                            if (mpeBusy[(size_t) ch] < mpeBusy[(size_t) best]) best = ch;
                        channel = best;
                        mpeBusy[(size_t) channel] = off;
                        const auto first = mpe::expressionAt (n, 0);
                        const int bend = mpe::bendValue (first.bend);
                        out.midi.push_back (message (on - 0.0005, 1, 0xE0 | channel, bend & 127, bend >> 7));
                        out.midi.push_back (message (on - 0.0005, 1, 0xB0 | channel, 74, (int) std::lround (first.timbre * 127)));
                        out.midi.push_back (message (on - 0.0005, 1, 0xD0 | channel, (int) std::lround (first.pressure * 127), 0, 2));
                        for (auto& e : n.expression)
                        {
                            if (e.tick <= 0 || a + e.tick >= b) continue;
                            const double s = tempo.secondsAt (a + e.tick) + delay;
                            const int v = mpe::bendValue (e.bend);
                            out.midi.push_back (message (s, 1, 0xE0 | channel, v & 127, v >> 7));
                            out.midi.push_back (message (s, 1, 0xD0 | channel, (int) std::lround (e.pressure * 127), 0, 2));
                            out.midi.push_back (message (s, 1, 0xB0 | channel, 74, (int) std::lround (e.timbre * 127)));
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
