#include "Song.h"

#include "Edit.h"

#include <algorithm>
#include <limits>
#include <map>

namespace kal::song
{

namespace
{
template <typename C, typename F> void eraseIf (C& c, F f) { c.erase (std::remove_if (c.begin(), c.end(), f), c.end()); }

std::vector<Track*> allTracks (Project& p)
{
    std::vector<Track*> out;
    for (auto& t : p.tracks) out.push_back (&t);
    out.push_back (&p.master);
    return out;
}

// Every clip list a track has: each version's clips and each take's.
template <typename F> void forClipLists (Project& p, F f)
{
    for (auto& t : p.tracks)
        for (auto& v : t.versions)
        {
            f (v.clips, &v);
            for (auto& tk : v.takes) f (tk.clips, nullptr);
        }
}

// Splits every clip in the list that crosses `at`.
void splitAt (Project& p, std::vector<Clip>& clips, Tick at)
{
    std::vector<Clip> added;
    for (auto& c : clips)
    {
        if (at <= c.start || at >= c.end()) continue;
        Clip right = c;
        right.id = p.newId();
        right.start = at;
        right.length = c.end() - at;
        right.offset = c.offset + (at - c.start);
        c.length = at - c.start;
        if (c.isMidi() && c.loopLength == 0)
        {
            const Tick cut = c.offset + c.length;
            eraseIf (c.midi.notes, [cut] (const Note& n) { return n.start >= cut; });
            eraseIf (right.midi.notes, [cut] (const Note& n) { return n.start < cut; });
            for (auto& n : right.midi.notes) n.id = p.newId();
        }
        right.audio.fadeIn = 0;
        c.audio.fadeOut = 0;
        added.push_back (right);
    }
    clips.insert (clips.end(), added.begin(), added.end());
    std::stable_sort (clips.begin(), clips.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
}

int barsIn (const TempoMap& tempo, Tick at, Tick length)
{
    const auto& m = tempo.meterAt (at);
    const Tick bar = (Tick) PPQ * 4 * m.numerator / m.denominator;
    if (bar <= 0 || length % bar != 0 || tempo.barStart (tempo.barAt (at)) != at) return 0;
    return (int) (length / bar);
}

// What's in [from, from + length) copied to start at `dest` (the content
// there was made room for already).
void copyContent (Project& p, Tick from, Tick length, Tick dest)
{
    const Tick to = from + length;
    const Tick shift = dest - from;
    forClipLists (p, [&] (std::vector<Clip>& clips, TrackVersion*)
    {
        std::vector<Clip> copies;
        for (auto& c : clips)
        {
            if (c.end() <= from || c.start >= to) continue;
            Clip x = c;
            x.id = p.newId();
            const Tick a = std::max (c.start, from), b = std::min (c.end(), to);
            x.offset += a - c.start;
            x.start = a + shift;
            x.length = b - a;
            x.locked = false;
            for (auto& n : x.midi.notes) n.id = p.newId();
            copies.push_back (x);
        }
        clips.insert (clips.end(), copies.begin(), copies.end());
        std::stable_sort (clips.begin(), clips.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
    });
    for (auto* t : allTracks (p))
        for (auto& lane : t->automation)
        {
            if (lane.points.empty()) continue;
            const double atFrom = edit::valueAt (lane, from, 0.5), atEnd = edit::valueAt (lane, to - 1, 0.5);
            std::vector<AutoPoint> add { { dest, atFrom }, { dest + length - 1, atEnd } };
            for (auto& pt : lane.points)
                if (pt.tick >= from && pt.tick < to) { auto q = pt; q.tick += shift; add.push_back (q); }
            for (auto& q : add) edit::addPoint (lane, q);
        }
    std::vector<Marker> markers;
    for (auto& m : p.markers)
        if (m.tick >= from && m.tick < to) markers.push_back ({ p.newId(), m.tick + shift, m.name });
    p.markers.insert (p.markers.end(), markers.begin(), markers.end());
    std::vector<Section> sections;
    for (auto& s : p.sections)
        if (s.start >= from && s.end <= to) sections.push_back ({ p.newId(), s.name, s.start + shift, s.end + shift, s.colour });
    p.sections.insert (p.sections.end(), sections.begin(), sections.end());
    sortSections (p);

    // The tempo inside goes with it, and what was after it comes back after.
    auto tempos = p.tempo.tempos();
    const double atFrom = p.tempo.bpmAt (from);
    const double afterCopy = p.tempo.bpmAt (dest + length);
    std::vector<TempoPoint> add;
    for (auto& t : tempos) if (t.tick > from && t.tick < to) { auto q = t; q.tick += shift; add.push_back (q); }
    bool hasStart = false;
    for (auto& t : tempos) if (t.tick == dest) hasStart = true;
    if (! hasStart && std::abs (p.tempo.bpmAt (dest) - atFrom) > 1e-9) add.push_back ({ dest, atFrom, false });
    tempos.insert (tempos.end(), add.begin(), add.end());
    if (std::abs (p.tempo.bpmAt (dest + length) - afterCopy) > 1e-9 || ! add.empty())
    {
        bool hasEnd = false;
        for (auto& t : tempos) if (t.tick == dest + length) hasEnd = true;
        if (! hasEnd) tempos.push_back ({ dest + length, afterCopy, false });
    }
    p.tempo.setTempos (tempos);
}
} // namespace

//==============================================================================
void insertTime (Project& p, Tick at, Tick length)
{
    if (length <= 0) return;
    const int bars = barsIn (p.tempo, at, length);
    const int atBar = p.tempo.barAt (at);
    forClipLists (p, [&] (std::vector<Clip>& clips, TrackVersion* v)
    {
        splitAt (p, clips, at);
        for (auto& c : clips) if (c.start >= at) c.start += length;
        if (v)
            for (auto& seg : v->comp)
            {
                if (seg.start >= at) seg.start += length;
                if (seg.end > at) seg.end += length;
            }
    });
    for (auto* t : allTracks (p))
        for (auto& lane : t->automation)
        {
            if (lane.points.empty()) continue;
            const double v = edit::valueAt (lane, at, 0.5);
            for (auto& pt : lane.points) if (pt.tick >= at) pt.tick += length;
            // The gap holds the value it had.
            edit::addPoint (lane, { at, v });
            edit::addPoint (lane, { at + length - 1, v });
        }
    for (auto& m : p.markers) if (m.tick >= at) m.tick += length;
    for (auto& s : p.sections)
    {
        if (s.start >= at) s.start += length;
        if (s.end > at) s.end += length;
    }
    auto tempos = p.tempo.tempos();
    for (auto& t : tempos) if (t.tick >= at && t.tick > 0) t.tick += length;
    p.tempo.setTempos (tempos);
    if (bars > 0)
    {
        auto meters = p.tempo.meters();
        for (auto& m : meters) if (m.bar >= atBar && m.bar > 0) m.bar += bars;
        p.tempo.setMeters (meters);
    }
    for (auto* r : { &p.loop, &p.punch })
    {
        if (r->start >= at) r->start += length;
        if (r->end > at) r->end += length;
    }
}

void removeTime (Project& p, Tick from, Tick to)
{
    if (to <= from) return;
    const Tick length = to - from;
    const int bars = barsIn (p.tempo, from, length);
    const int fromBar = p.tempo.barAt (from);
    forClipLists (p, [&] (std::vector<Clip>& clips, TrackVersion* v)
    {
        splitAt (p, clips, from);
        splitAt (p, clips, to);
        eraseIf (clips, [&] (const Clip& c) { return c.start >= from && c.end() <= to; });
        for (auto& c : clips) if (c.start >= to) c.start -= length;
        if (v)
        {
            for (auto& seg : v->comp)
            {
                auto cut = [&] (Tick t) { return t <= from ? t : t >= to ? t - length : from; };
                seg.start = cut (seg.start);
                seg.end = cut (seg.end);
            }
            eraseIf (v->comp, [] (const CompSegment& s) { return s.end <= s.start; });
        }
    });
    for (auto* t : allTracks (p))
        for (auto& lane : t->automation)
        {
            if (lane.points.empty()) continue;
            const double before = edit::valueAt (lane, from, 0.5), after = edit::valueAt (lane, to, 0.5);
            eraseIf (lane.points, [&] (const AutoPoint& pt) { return pt.tick >= from && pt.tick < to; });
            for (auto& pt : lane.points) if (pt.tick >= to) pt.tick -= length;
            if (from > 0) edit::addPoint (lane, { from - 1, before });
            edit::addPoint (lane, { from, after });
        }
    eraseIf (p.markers, [&] (const Marker& m) { return m.tick >= from && m.tick < to; });
    for (auto& m : p.markers) if (m.tick >= to) m.tick -= length;
    eraseIf (p.sections, [&] (const Section& s) { return s.start >= from && s.end <= to; });
    for (auto& s : p.sections)
    {
        auto cut = [&] (Tick t) { return t <= from ? t : t >= to ? t - length : from; };
        s.start = cut (s.start);
        s.end = cut (s.end);
    }
    eraseIf (p.sections, [] (const Section& s) { return s.end <= s.start; });

    const double bpmAfter = p.tempo.bpmAt (to);
    auto tempos = p.tempo.tempos();
    eraseIf (tempos, [&] (const TempoPoint& t) { return t.tick >= from && t.tick < to && t.tick > 0; });
    for (auto& t : tempos) if (t.tick >= to) t.tick -= length;
    bool hasFrom = false;
    for (auto& t : tempos) if (t.tick == from) hasFrom = true;
    p.tempo.setTempos (tempos);
    if (! hasFrom && std::abs (p.tempo.bpmAt (from) - bpmAfter) > 1e-9) p.tempo.setTempoAt (from, bpmAfter);
    if (bars > 0)
    {
        auto meters = p.tempo.meters();
        eraseIf (meters, [&] (const Meter& m) { return m.bar >= fromBar && m.bar < fromBar + bars && m.bar > 0; });
        for (auto& m : meters) if (m.bar >= fromBar + bars) m.bar -= bars;
        p.tempo.setMeters (meters);
    }
    for (auto* r : { &p.loop, &p.punch })
    {
        auto cut = [&] (Tick t) { return t <= from ? t : t >= to ? t - length : from; };
        r->start = cut (r->start);
        r->end = cut (r->end);
        if (r->end <= r->start) r->end = r->start + PPQ * 4;
    }
}

Tick duplicateTime (Project& p, Tick from, Tick to)
{
    if (to <= from) return to;
    insertTime (p, to, to - from);
    copyContent (p, from, to - from, to);
    return to;
}

void moveTime (Project& p, Tick from, Tick to, Tick dest)
{
    if (to <= from || (dest >= from && dest <= to)) return;
    const Tick length = to - from;
    insertTime (p, dest, length);
    const Tick src = from >= dest ? from + length : from;
    copyContent (p, src, length, dest);
    removeTime (p, src, src + length);
}

//==============================================================================
void sortSections (Project& p)
{
    std::stable_sort (p.sections.begin(), p.sections.end(), [] (const Section& a, const Section& b) { return a.start < b.start; });
}

const Section* sectionAt (const Project& p, Tick t)
{
    for (auto& s : p.sections) if (t >= s.start && t < s.end) return &s;
    return nullptr;
}

Id addSection (Project& p, const std::string& name, Tick start, Tick end)
{
    if (end <= start) return 0;
    // Sections don't overlap: the new one trims its neighbours.
    for (auto& s : p.sections)
    {
        if (s.start < start && s.end > start) s.end = start;
        else if (s.start >= start && s.start < end) s.start = end;
    }
    eraseIf (p.sections, [] (const Section& s) { return s.end <= s.start; });
    Section s;
    s.id = p.newId();
    s.name = name;
    s.start = start;
    s.end = end;
    p.sections.push_back (s);
    sortSections (p);
    return s.id;
}

void removeSection (Project& p, Id id, bool withContents)
{
    for (auto& s : p.sections)
        if (s.id == id)
        {
            const auto [a, b] = std::pair { s.start, s.end };
            if (withContents) removeTime (p, a, b);
            eraseIf (p.sections, [id] (const Section& x) { return x.id == id; });
            return;
        }
}

Id duplicateSection (Project& p, Id id)
{
    for (auto& s : p.sections)
        if (s.id == id)
        {
            const Tick a = s.start, b = s.end;
            duplicateTime (p, a, b);
            for (auto& x : p.sections) if (x.start == b && x.id != id) return x.id;
            return 0;
        }
    return 0;
}

void moveSection (Project& p, Id id, Id before)
{
    const Section* s = nullptr;
    for (auto& x : p.sections) if (x.id == id) s = &x;
    if (! s) return;
    Tick dest = 0;
    if (before == 0) { for (auto& x : p.sections) dest = std::max (dest, x.end); }
    else
    {
        bool found = false;
        for (auto& x : p.sections) if (x.id == before) { dest = x.start; found = true; }
        if (! found) return;
    }
    const Tick length = s->end - s->start;
    const std::string name = s->name;
    const Tick landed = dest < s->start ? dest : dest - length;
    moveTime (p, s->start, s->end, dest);
    for (auto& x : p.sections)
        if (x.start == landed && x.end == landed + length && x.name == name) { x.id = id; break; }
}

void moveSectionTo (Project& p, Id id, Tick start)
{
    const Section* s = nullptr;
    for (auto& x : p.sections) if (x.id == id) s = &x;
    if (! s) return;
    start = std::max<Tick> (0, start);
    if (start == s->start) return;
    // moveTime puts the stretch before `dest` as things are now: later, that's past where it's taken from.
    const Tick length = s->end - s->start;
    const std::string name = s->name;
    const Tick dest = start < s->start ? start : start + length;
    moveTime (p, s->start, s->end, dest);
    // The section moved is the same section: it keeps its id.
    for (auto& x : p.sections)
        if (x.start == start && x.end == start + length && x.name == name) { x.id = id; break; }
}

//==============================================================================
void rippleRemove (Project& p, const std::set<Id>& ids)
{
    struct Gone { Track* track; Tick start, end; Id id; };
    std::vector<Gone> gone;
    for (auto id : ids)
    {
        Track* owner = nullptr;
        if (auto* c = p.clip (id, &owner); c && ! c->locked) gone.push_back ({ owner, c->start, c->end(), id });
    }
    // Latest first, so each shift is by its own clip's length.
    std::sort (gone.begin(), gone.end(), [] (const Gone& a, const Gone& b) { return a.start > b.start; });
    for (auto& g : gone)
    {
        auto& clips = g.track->clips();
        eraseIf (clips, [&] (const Clip& c) { return c.id == g.id; });
        for (auto& c : clips) if (c.start >= g.end) c.start -= g.end - g.start;
    }
}

//==============================================================================
Id duplicateLinked (Project& p, Id clipId)
{
    auto* c = p.clip (clipId);
    if (! c) return 0;
    if (c->link == 0) c->link = p.newId();
    const Id link = c->link;
    const Id copy = edit::duplicateClip (p, clipId);
    if (auto* x = p.clip (copy)) x->link = link;
    return copy;
}

void unlink (Project& p, Id clipId)
{
    if (auto* c = p.clip (clipId)) c->link = 0;
}

void propagateLinks (const Project& before, Project& after)
{
    std::map<Id, const Clip*> old;
    for (auto& t : before.tracks)
        for (auto& v : t.versions)
            for (auto& c : v.clips) if (c.link) old[c.id] = &c;
    std::map<Id, const Clip*> source;   // link -> the clip that changed
    for (auto& t : after.tracks)
        for (auto& v : t.versions)
            for (auto& c : v.clips)
            {
                if (! c.link || source.count (c.link)) continue;
                auto it = old.find (c.id);
                if (it == old.end()) continue;
                const auto& o = *it->second;
                if (o.midi != c.midi || o.audio != c.audio || o.offlineFx != c.offlineFx) source[c.link] = &c;
            }
    if (source.empty()) return;
    // Copied first: the sources live in the lists being written.
    std::map<Id, Clip> content;
    for (auto& [link, c] : source) content[link] = *c;
    for (auto& t : after.tracks)
        for (auto& v : t.versions)
            for (auto& c : v.clips)
            {
                auto it = content.find (c.link);
                if (! c.link || it == content.end() || it->second.id == c.id) continue;
                c.midi = it->second.midi;
                c.audio = it->second.audio;
                c.offlineFx = it->second.offlineFx;
            }
}

//==============================================================================
std::vector<TrackMix> mixOf (const Project& p)
{
    std::vector<TrackMix> out;
    for (auto& t : p.tracks) out.push_back ({ t.id, t.group, t.volumeDb, t.pan, t.mute, t.solo, t.armed, t.armFor });
    return out;
}

void propagateGroups (const std::vector<TrackMix>& before, Project& after)
{
    std::map<Id, TrackMix> old;
    for (auto& m : before) old[m.id] = m;
    std::map<int, Track*> leader;
    for (auto& t : after.tracks)
    {
        if (t.group == 0 || leader.count (t.group)) continue;
        auto it = old.find (t.id);
        if (it == old.end() || it->second.group != t.group) continue;
        const auto& o = it->second;
        if (o.volumeDb != t.volumeDb || o.pan != t.pan || o.mute != t.mute || o.solo != t.solo || o.armed != t.armed || o.armFor != t.armFor)
            leader[t.group] = &t;
    }
    for (auto& [group, lead] : leader)
    {
        const auto& o = old[lead->id];
        for (auto& t : after.tracks)
        {
            if (&t == lead || t.group != group) continue;
            auto it = old.find (t.id);
            if (it == old.end()) continue;
            const auto& mine = it->second;
            // Only what this track hasn't changed itself.
            if (o.volumeDb != lead->volumeDb && mine.volumeDb == t.volumeDb)
                t.volumeDb = std::clamp (t.volumeDb + (lead->volumeDb - o.volumeDb), -100.0, 12.0);
            if (o.pan != lead->pan && mine.pan == t.pan) t.pan = std::clamp (t.pan + (lead->pan - o.pan), -1.0, 1.0);
            if (o.mute != lead->mute) t.mute = lead->mute;
            if (o.solo != lead->solo) t.solo = lead->solo;
            if ((o.armed != lead->armed || o.armFor != lead->armFor) && t.holdsClips())
            {
                t.armed = lead->armed;
                t.armFor = t.kind == TrackKind::Summing ? ArmFor::Audio : lead->armFor;
            }
        }
    }
}

//==============================================================================
int sceneCount (const Project& p)
{
    size_t n = p.scenes.size();
    for (auto& t : p.tracks) n = std::max (n, t.slots.size());
    return (int) n;
}

void setSlot (Project& p, Id trackId, int slot, Clip c)
{
    auto* t = p.track (trackId);
    if (! t || slot < 0 || ! t->holdsClips()) return;
    if ((int) t->slots.size() <= slot) t->slots.resize ((size_t) slot + 1, Clip { .length = 0 });
    c.id = p.newId();
    for (auto& n : c.midi.notes) n.id = p.newId();
    t->slots[(size_t) slot] = c;
    while ((int) p.scenes.size() < sceneCount (p)) p.scenes.push_back ("Scene " + std::to_string (p.scenes.size() + 1));
}

void clearSlot (Project& p, Id trackId, int slot)
{
    auto* t = p.track (trackId);
    if (! t || slot < 0 || slot >= (int) t->slots.size()) return;
    t->slots[(size_t) slot] = Clip { .length = 0 };
    if (t->launch.slot == slot) t->launch = {};
}

int captureScene (Project& p, Tick from, Tick to, const std::string& name)
{
    if (to <= from) return -1;
    const int scene = sceneCount (p);
    for (auto& t : p.tracks)
    {
        if (! t.holdsClips()) continue;
        Clip midi;
        midi.kind = Clip::Kind::Midi;
        midi.start = from;
        midi.length = to - from;
        midi.loopLength = to - from;
        bool anyMidi = false;
        const Clip* audio = nullptr;
        for (auto& c : t.clips())
        {
            if (c.end() <= from || c.start >= to || c.muted) continue;
            if (! c.isMidi()) { if (! audio) audio = &c; continue; }
            anyMidi = true;
            if (midi.name.empty()) midi.name = c.name;
            // Notes playing in the range, from the scene's start.
            const Tick contentStart = c.start - c.offset;
            for (auto n : c.midi.notes)
            {
                const Tick a = contentStart + n.start;
                if (a < std::max (from, c.start) || a >= std::min (to, c.end())) continue;
                n.start = a - from;
                n.length = std::min (n.length, to - a);
                midi.midi.notes.push_back (n);
            }
        }
        if (anyMidi)
        {
            edit::sortNotes (midi.midi);
            setSlot (p, t.id, scene, midi);
        }
        else if (audio)
        {
            Clip x = *audio;
            const Tick a = std::max (x.start, from), b = std::min (x.end(), to);
            x.offset += a - x.start;
            x.start = a;
            x.length = b - a;
            setSlot (p, t.id, scene, x);
        }
    }
    if ((int) p.scenes.size() <= scene) p.scenes.resize ((size_t) scene + 1);
    p.scenes[(size_t) scene] = name;
    return scene;
}

void launch (Project& p, Id trackId, int slot, Tick now)
{
    auto* t = p.track (trackId);
    if (! t || ! t->holdsClips()) return;
    const int bar = p.tempo.barAt (now);
    const Tick from = p.tempo.barStart (bar) == now ? now : p.tempo.barStart (bar + 1);
    if (slot >= 0 && (slot >= (int) t->slots.size() || t->slots[(size_t) slot].length <= 0)) return;
    t->launch.slot = slot;
    t->launch.from = from;
    t->launch.stopped = slot < 0;
}

void launchScene (Project& p, int scene, Tick now)
{
    for (auto& t : p.tracks)
    {
        if (! t.holdsClips()) continue;
        const bool filled = scene < (int) t.slots.size() && t.slots[(size_t) scene].length > 0;
        launch (p, t.id, filled ? scene : -1, now);
    }
}

void backToArrangement (Project& p)
{
    for (auto& t : p.tracks) t.launch = {};
}

bool anyLaunched (const Project& p)
{
    for (auto& t : p.tracks) if (t.launch.slot >= 0 || t.launch.stopped) return true;
    return false;
}

bool loopToClips (Project& p, const std::set<Id>& clips)
{
    Tick from = std::numeric_limits<Tick>::max(), to = 0;
    for (auto id : clips)
        if (const auto* c = p.clip (id)) { from = std::min (from, c->start); to = std::max (to, c->end()); }
    if (to <= from) return false;
    p.loop.start = from;
    p.loop.end = to;
    p.loop.on = true;
    return true;
}

void detectChords (Project& p, const std::set<Id>& clips, bool on)
{
    for (auto id : clips)
        if (auto* c = p.clip (id); c && c->isMidi()) c->detectChords = on;
}

} // namespace kal::song
