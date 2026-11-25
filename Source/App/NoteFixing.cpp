// More of the Controller: Fix Note Timings and Volume (decision 0085),
// Note Position & Volume Fixer built in. Core/NoteFixer measures; this
// renders the track, hands it the notes as they were sent, and writes back
// what it found.
#include "Controller.h"

#include "NoteFixer.h"
#include "Theme.h"

namespace kal
{
using namespace juce;

namespace
{
// A note as it plays: which note, and where (ticks on the timeline, the first pass of a loop first).
struct Occurrence { Id note; Tick at, end; };

double median (std::vector<double> v)
{
    if (v.empty()) return 0;
    std::sort (v.begin(), v.end());
    return v.size() % 2 ? v[v.size() / 2] : (v[v.size() / 2 - 1] + v[v.size() / 2]) / 2;
}
} // namespace

bool Controller::fixNoteTimingsAndVolume (Id clipId, const NoteFixOptions& o)
{
    const Track* owner = nullptr;
    const auto* clip = project.clip (clipId, &owner);
    if (! clip || ! owner || ! clip->isMidi()) { say ("Choose a MIDI clip to fix.", true); return false; }
    if (owner->frozen) { say ("Unfreeze " + String (owner->name) + " to fix its notes: its instruments are unloaded.", true); return false; }
    if (clip->midi.notes.empty()) { say ("There are no notes in this clip to fix.", true); return false; }
    const Id trackId = owner->id;
    const auto& tempo = project.tempo;
    const auto* map = project.articulationMap (owner->articulationMap);
    auto articulation = [&] (const Note& n) -> const Articulation*
    {
        return map && n.articulation >= 0 && n.articulation < (int) map->articulations.size() ? &map->articulations[(size_t) n.articulation] : nullptr;
    };
    auto otherDelayMs = [&] (const Note& n) { const auto* a = articulation (n); return (a ? a->delayMs : 0.0) + n.delayMs; };

    // Every note as it plays, each pass of a loop.
    const Tick contentStart = clip->start - clip->offset;
    const Tick passLength = clip->loopLength > 0 ? clip->loopLength : std::numeric_limits<Tick>::max() / 4;
    std::vector<Occurrence> plays;
    int random = 0;
    for (Tick base = contentStart; base < clip->end(); base += passLength)
    {
        for (auto& n : clip->midi.notes)
        {
            if (clip->loopLength > 0 && n.start >= clip->loopLength) continue;
            const Tick a = base + n.start;
            if (a < clip->start || a >= clip->end()) continue;
            // Played differently each time (decision 0061): nothing steady to measure.
            if (n.chance < 100 || n.timingMs > 0 || n.velocityRange > 0) { ++random; continue; }
            Tick b = std::min (a + n.length, clip->end());
            if (clip->loopLength > 0) b = std::min (b, base + clip->loopLength);
            plays.push_back ({ n.id, a, std::max (a + 1, b) });
        }
        if (clip->loopLength <= 0) break;
    }
    if (plays.empty()) { say ("Every note in this clip plays at random (chance or random timing): there's nothing steady to fix.", true); return false; }

    auto find = [&] (Id id) -> const Note* { for (auto& n : clip->midi.notes) if (n.id == id) return &n; return nullptr; };
    // When each is sent, with the track's, its articulation's and its own delay.
    double first = 1e300, last = -1e300;
    for (auto& pl : plays)
    {
        const auto* n = find (pl.note);
        const double d = playback::noteDelaySeconds (project, *owner, *n);
        first = std::min (first, tempo.secondsAt (pl.at) + d);
        last = std::max (last, tempo.secondsAt (pl.end) + d);
    }
    const double from = first - 0.25;

    // Rendered quietly, as Freeze would (the instruments as they are, nothing unloaded),
    // read and deleted. Twice (decision 0090): as written, for the timing (a library's
    // delay depends on how hard a note is played); and for the volume, every note at
    // velocity 80 with the clip's controllers left out (a few neutral ones set at its
    // start), so a dip written with CC7, a swell in CC1 or a soft velocity isn't taken
    // for a sample that's too quiet. Both are changes to the copy rendered, never to the
    // song: nothing needs putting back.
    say (String ("Listening to ") + String (owner->name) + ui::u8 ("\xe2\x80\xa6"));
    double rate = engine.sampleRate();
    auto listen = [&] (bool even, std::vector<float>& mono) -> bool
    {
        const auto render = File::getSpecialLocation (File::tempDirectory).getNonexistentChildFile ("Kalibre note fix", ".wav");
        String error;
        auto evenOut = [clipId] (Project& p)
        {
            auto* c = p.clip (clipId);
            if (! c) return;
            for (auto& n : c->midi.notes) n.velocity = 80;
            std::set<int> channels;
            for (auto& n : c->midi.notes) channels.insert (n.channel & 15);
            std::erase_if (c->midi.events, [] (const MidiEvent& e)
            {
                return e.type == EventType::Controller || e.type == EventType::ChannelPressure || e.type == EventType::PolyPressure;
            });
            // An instrument keeps the last value it was sent: these set every note's level alike.
            const Tick at = std::max<Tick> (0, c->offset);
            for (int ch : channels)
                for (const auto& cc : { std::pair { 1, 80 }, { 7, 100 }, { 11, 127 } })   // not bound by name: MSVC wants those captured
                    c->midi.events.push_back ({ at, EventType::Controller, ch, cc.first, cc.second });
            std::stable_sort (c->midi.events.begin(), c->midi.events.end(), [] (const MidiEvent& x, const MidiEvent& y) { return x.tick < y.tick; });
        };
        if (! renderTrackAudio (trackId, { clipId }, from, last + 0.1, 1.5, render, error, true, even ? std::function<void (Project&)> (evenOut) : nullptr))
        {
            render.deleteFile();
            say ("The track couldn't be rendered to fix its notes. " + error, true);
            return false;
        }
        AudioFormatManager fm;
        fm.registerBasicFormats();
        if (std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (render)); r)
        {
            rate = r->sampleRate;
            AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
            r->read (&b, 0, b.getNumSamples(), 0, true, true);
            mono.resize ((size_t) b.getNumSamples());
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                float v = 0;
                for (int ch = 0; ch < b.getNumChannels(); ++ch) v += b.getSample (ch, i);
                mono[(size_t) i] = v / (float) jmax (1, b.getNumChannels());
            }
        }
        render.deleteFile();   // nothing is left behind
        if (mono.empty()) { say ("The track's render couldn't be read.", true); return false; }
        return true;
    };
    const bool wantTiming = o.timing != NoteFixOptions::Timing::Off, wantVolume = o.volume != NoteFixOptions::Volume::Off;
    std::vector<float> asWritten, evened;
    if (wantTiming && ! listen (false, asWritten)) return false;
    if (wantVolume && ! listen (true, evened)) return false;

    // The notes as the fixer sees them: in the render's seconds.
    auto ccInForce = [&] (int number, int channel, Tick contentTick) -> int
    {
        int v = -1;
        Tick at = std::numeric_limits<Tick>::min();
        for (auto& e : clip->midi.events)
            if (e.type == EventType::Controller && e.number == number && (e.channel & 15) == channel && e.tick <= contentTick && e.tick >= at) { v = e.value; at = e.tick; }
        return v;
    };
    std::vector<notefix::NoteIn> in;
    for (auto& pl : plays)
    {
        const auto* n = find (pl.note);
        const double d = playback::noteDelaySeconds (project, *owner, *n);
        notefix::NoteIn x;
        x.pitch = n->pitch;
        x.velocity = n->velocity;
        x.seconds = tempo.secondsAt (pl.at) + d - from;
        x.endSeconds = tempo.secondsAt (pl.end) + d - from;
        x.articulation = n->articulation;
        x.cc1 = ccInForce (1, n->channel & 15, n->start);
        in.push_back (x);
    }
    notefix::Options ao;
    ao.alignToAttack = o.toAttack;
    notefix::PlanOptions po;
    // Timing from the render as written.
    notefix::Analysis timingAnalysis;
    std::vector<notefix::NoteFix> timingFixes (in.size());
    if (wantTiming)
    {
        timingAnalysis = notefix::analyse (asWritten, rate, in, ao);
        po.volume = notefix::PlanOptions::Volume::Off;
        timingFixes = notefix::plan (timingAnalysis, in, po);
    }
    // Volume from the evened render: every note at 80, no controllers.
    notefix::Analysis volumeAnalysis;
    std::vector<notefix::NoteFix> volumeFixes (in.size());
    if (wantVolume)
    {
        auto even = in;
        for (auto& x : even) { x.velocity = 80; x.cc1 = -1; }
        volumeAnalysis = notefix::analyse (evened, rate, even, ao);
        po.volume = notefix::PlanOptions::Volume::EachNote;
        po.timing = notefix::PlanOptions::Timing::Off;
        volumeFixes = notefix::plan (volumeAnalysis, even, po);
    }
    const auto& analysis = wantTiming ? timingAnalysis : volumeAnalysis;   // for what the message line says

    // Each note: the middle of what every pass of it found.
    struct Found { std::vector<double> shift, gain; bool solo = true; int chord = -1; };
    std::map<Id, Found> byNote;
    for (size_t i = 0; i < plays.size(); ++i)
    {
        const bool timed = wantTiming && timingAnalysis.notes[i].found;
        const bool levelled = wantVolume && volumeAnalysis.notes[i].found;
        if (! timed && ! levelled) continue;
        const auto& m = levelled ? volumeAnalysis.notes[i] : timingAnalysis.notes[i];
        auto& f = byNote[plays[i].note];
        if (timed) f.shift.push_back (timingFixes[i].shiftMs);
        if (levelled) f.gain.push_back (volumeFixes[i].gainDb);
        // Sounding alone or as a chord, its level can be set with a controller; under or
        // over another note (legato), the controller would move that note too.
        if (m.overlapped && m.chord < 0) f.solo = false;
        f.chord = m.chord;
    }
    if (byNote.empty())
    {
        say ("No notes were found in the render (is there an instrument on the track, and does it make sound?). Nothing was changed.", true);
        return false;
    }
    // dB a velocity step: only the render as written has different velocities to fit it from.
    const double dbPerVelocity = std::isfinite (timingAnalysis.dbPerVelocity) ? timingAnalysis.dbPerVelocity : 0.25;
    const bool moveNotes = o.timing == NoteFixOptions::Timing::MoveNotes && clip->loopLength == 0;
    const int ccNumber = o.volume == NoteFixOptions::Volume::Cc11 ? 11 : 7;
    const bool byController = o.volume == NoteFixOptions::Volume::Cc7 || o.volume == NoteFixOptions::Volume::Cc11;

    // The level each note was heard at: the controller in force when it was sent, before any change.
    std::map<Id, int> heardLevel;
    for (auto& n : clip->midi.notes)
    {
        const int ch = [&] { const auto* a = articulation (n); return a && a->channel >= 0 ? a->channel & 15 : n.channel & 15; }();
        const Tick sent = tempo.tickAt (tempo.secondsAt (contentStart + n.start) + otherDelayMs (n) / 1000.0) - contentStart;
        heardLevel[n.id] = ccInForce (o.volume == NoteFixOptions::Volume::Cc11 ? 11 : 7, ch, sent);
    }
    int moved = 0, levelled = 0, byVelocity = 0;
    const String trackName (owner->name);
    const int notFound = (int) clip->midi.notes.size() - (int) byNote.size();
    apply ("Fix Note Timings and Volume", [&] (Project& p)
    {
        auto* c = p.clip (clipId);
        const auto* t = p.track (trackId);
        if (! c || ! t) return;
        const Tick cs = c->start - c->offset;
        // Timing. What was found is how late each sound is after the note was sent;
        // from its written place it is that plus every delay it was sent with (the
        // track's, its articulation's, its own). Each is set so the sound lands on
        // the written place, so fixing again changes nothing.
        std::map<Id, double> shift;
        for (auto& [id, f] : byNote)
            if (const auto* n = find (id); n && ! f.shift.empty()) shift[id] = median (f.shift) - playback::noteDelaySeconds (project, *owner, *n) * 1000.0;
        if (o.timing != NoteFixOptions::Timing::Off)
        {
            Tick earliest = std::numeric_limits<Tick>::max();
            for (auto& n : c->midi.notes)
            {
                auto it = shift.find (n.id);
                if (it == shift.end() || std::abs (it->second) < 0.5) continue;
                ++moved;
                if (moveNotes)
                {
                    // Moved on the timeline, its length kept.
                    const Tick abs = cs + n.start;
                    const Tick to = tempo.tickAt (tempo.secondsAt (abs) + it->second / 1000.0);
                    n.start = to - cs;
                    earliest = std::min (earliest, n.start);
                }
                else n.delayMs = std::round ((n.delayMs + it->second) * 10.0) / 10.0;   // where it plays, still written on its place
            }
            // Notes now before the clip's start: the clip starts earlier to keep them.
            if (moveNotes && earliest < c->offset)
            {
                Tick need = c->offset - earliest;
                need = std::min (need, c->start);
                if (c->offset >= need) c->offset -= need;
                else
                {
                    const Tick k = need - c->offset;
                    for (auto& n : c->midi.notes) n.start += k;
                    for (auto& e : c->midi.events) e.tick += k;
                    c->offset = 0;
                }
                c->start -= need;
                c->length += need;
            }
        }
        if (o.volume == NoteFixOptions::Volume::Off) return;

        // Volume. Notes of one moment (a chord) share one level.
        const Tick csNow = c->start - c->offset;
        std::map<Tick, std::vector<Note*>> moments;
        for (auto& n : c->midi.notes) if (byNote.count (n.id)) moments[n.start].push_back (&n);
        // When each note is really sent, in ticks without the track's delay (as controllers are placed).
        auto sentTick = [&] (const Note& n)
        {
            const Tick abs = csNow + n.start;
            const auto* m = p.articulationMap (t->articulationMap);
            const double art = m && n.articulation >= 0 && n.articulation < (int) m->articulations.size() ? m->articulations[(size_t) n.articulation].delayMs : 0.0;
            return tempo.tickAt (tempo.secondsAt (abs) + (art + n.delayMs) / 1000.0);
        };
        auto channelOf = [&] (const Note& n)
        {
            const auto* m = p.articulationMap (t->articulationMap);
            if (m && n.articulation >= 0 && n.articulation < (int) m->articulations.size() && m->articulations[(size_t) n.articulation].channel >= 0)
                return m->articulations[(size_t) n.articulation].channel & 15;
            return n.channel & 15;
        };
        Tick previousSent = std::numeric_limits<Tick>::min();
        for (auto& [start, notes] : moments)
        {
            std::vector<double> gains;
            bool solo = true;
            Tick sent = std::numeric_limits<Tick>::max();
            for (auto* n : notes)
            {
                gains.push_back (median (byNote[n->id].gain));
                solo = solo && byNote[n->id].solo;
                sent = std::min (sent, sentTick (*n));
            }
            const double gain = median (gains);
            const Tick ccAt = std::max (tempo.tickAt (tempo.secondsAt (sent) - 0.002), previousSent + 1);
            const int ch = channelOf (*notes.front());
            if (byController && solo && ccAt < sent && ccAt >= csNow)
            {
                // The level in force (what the render heard), turned up or down from there; the
                // controllers between the last note and this one (an earlier fix) are replaced.
                const int heard = heardLevel[notes.front()->id];
                const int current = heard >= 0 ? heard : 100;   // none: the General MIDI default
                std::erase_if (c->midi.events, [&] (const MidiEvent& e)
                {
                    const Tick abs = e.tick + csNow;
                    return e.type == EventType::Controller && e.number == ccNumber && (e.channel & 15) == ch && abs > previousSent && abs <= sent;
                });
                MidiEvent e;
                e.tick = ccAt - csNow;
                e.type = EventType::Controller;
                e.channel = ch;
                e.number = ccNumber;
                e.value = notefix::ccForDb (notefix::ccToDb (current) + gain);
                c->midi.events.push_back (e);
                if (std::abs (gain) > 0) levelled += (int) notes.size();
            }
            else if (gain != 0)
            {
                // Velocity: a softer or harder note (it may also choose another sample).
                for (auto* n : notes)
                {
                    const double g = median (byNote[n->id].gain);
                    if (g == 0) continue;
                    n->velocity = std::clamp ((int) std::lround (n->velocity + g / dbPerVelocity), 1, 127);
                    ++byVelocity;
                }
            }
            previousSent = std::max (previousSent, sent);
        }
        std::stable_sort (c->midi.events.begin(), c->midi.events.end(), [] (const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
    });

    String said = "Fixed " + trackName + ": the notes were late by " + String (roundToInt (analysis.medianDelayMs)) + " ms in the middle, "
                + String (roundToInt (analysis.spreadMs)) + " ms from earliest to latest";
    if (wantVolume && std::isfinite (volumeAnalysis.loudnessSpreadDb) && volumeAnalysis.loudnessSpreadDb > 0)
        said << ", their samples' loudness " << String (volumeAnalysis.loudnessSpreadDb, 1) << " dB apart (heard at one velocity, the clip's controllers left out)";
    said << ". ";
    if (o.timing != NoteFixOptions::Timing::Off)
        said << String (moved) << (moveNotes ? " moved" : " given their own delay (Note Delay), still on the grid") << "; ";
    if (byController) said << String (levelled) << " evened out with CC" << ccNumber;
    if (byVelocity > 0) said << (byController ? ", " : "") << String (byVelocity) << " by velocity";
    if (o.volume != NoteFixOptions::Volume::Off) said << ". ";
    if (notFound > 0) said << String (notFound) << (notFound == 1 ? " note wasn't" : " notes weren't") << " found and were left alone. ";
    if (random > 0) said << String (random) << " played at random were left alone. ";
    if (o.timing == NoteFixOptions::Timing::MoveNotes && ! moveNotes) said << "A looped clip's notes are given delays instead of moved. ";
    say (said.trim(), false, true);
    return true;
}

} // namespace kal
