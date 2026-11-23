#include "Edit.h"

#include "AudioTools.h"
#include "ProjectFile.h"

#include <cmath>
#include <limits>
#include <map>
#include <random>

namespace kal::edit
{

namespace
{
template <typename Vec, typename Pred>
void eraseIf (Vec& v, Pred p) { v.erase (std::remove_if (v.begin(), v.end(), p), v.end()); }

Clip* midiClip (Project& p, Id clip)
{
    auto* c = p.clip (clip);
    return (c && c->isMidi()) ? c : nullptr;
}

// The index just past a track's last descendant.
size_t blockEnd (const Project& p, size_t index)
{
    const Id id = p.tracks[index].id;
    size_t j = index + 1;
    while (j < p.tracks.size() && p.isInside (p.tracks[j].id, id)) ++j;
    return j;
}

void renumber (Project& p, Track& t)
{
    t.id = p.newId();
    for (auto& v : t.versions)
    {
        v.id = p.newId();
        for (auto& c : v.clips)
        {
            c.id = p.newId();
            for (auto& n : c.midi.notes) n.id = p.newId();
        }
    }
    std::map<Id, Id> deviceIds;
    for (auto& d : t.devices) { const Id old = d.id; d.id = p.newId(); deviceIds[old] = d.id; }
    // The same device in another version keeps sharing its new id (decision 0050).
    for (auto& v : t.versions)
        for (auto& d : v.devices)
        {
            if (! deviceIds.count (d.id)) deviceIds[d.id] = p.newId();
            d.id = deviceIds[d.id];
        }
    for (auto& a : t.automation)
    {
        a.id = p.newId();
        if (deviceIds.count (a.target.device)) a.target.device = deviceIds[a.target.device];
    }
    for (auto& m : t.modulators)
    {
        m.id = p.newId();
        for (auto& target : m.targets)
            if (deviceIds.count (target.device)) target.device = deviceIds[target.device];
    }
}
} // namespace

//==============================================================================
Id addCue (Project& p)
{
    if (! p.cue.empty()) return p.cue.front().id;
    Track t;
    t.id = p.newId();
    t.kind = TrackKind::Cue;
    t.name = "Cue";
    t.colour = 0xff6FA8DC;
    t.input.audioChannel = -1;
    t.input.midiDevice = "None";
    t.versions.front().id = p.newId();
    t.versions.front().name = "Version 1";
    t.versions.front().ownDevices = true;
    p.cue.push_back (t);
    return t.id;
}

Id addTrack (Project& p, TrackKind kind, const std::string& name, Id after, Id parent)
{
    if (kind == TrackKind::Cue) return addCue (p);
    Track t;
    t.id = p.newId();
    t.kind = kind == TrackKind::Master ? TrackKind::Track : kind;
    t.name = name;
    t.versions.front().id = p.newId();
    t.versions.front().name = "Version 1";
    t.versions.front().ownDevices = true;
    if (kind == TrackKind::Folder) t.height = 40;
    // A Summing track records the master, not an input (decision 0052).
    if (kind == TrackKind::Summing) { t.input.audioChannel = -1; t.input.midiDevice = "None"; t.colour = 0xffE3C26F; }
    if (kind == TrackKind::Return) t.colour = 0xff8F96A2;
    if (parent != 0 && (! p.track (parent) || ! p.track (parent)->isFolder())) parent = 0;
    t.parent = parent;

    size_t at = p.tracks.size();
    if (after != 0)
    {
        const int i = p.indexOf (after);
        if (i >= 0) at = blockEnd (p, static_cast<size_t> (i));
    }
    else if (parent != 0)
    {
        at = blockEnd (p, static_cast<size_t> (p.indexOf (parent)));
    }
    p.tracks.insert (p.tracks.begin() + static_cast<std::ptrdiff_t> (at), t);
    return t.id;
}

std::vector<Id> descendants (const Project& p, Id folder)
{
    std::vector<Id> out;
    for (auto& t : p.tracks) if (p.isInside (t.id, folder)) out.push_back (t.id);
    return out;
}

void removeTrack (Project& p, Id id, bool keepContents)
{
    auto* t = p.track (id);
    if (! t || t->kind == TrackKind::Master) return;
    if (t->isCue()) { p.cue.clear(); return; }
    const Id parent = t->parent;
    std::set<Id> gone { id };
    if (keepContents)
    {
        for (auto& c : p.tracks) if (c.parent == id) c.parent = parent;
    }
    else
    {
        for (auto d : descendants (p, id)) gone.insert (d);
    }
    eraseIf (p.tracks, [&] (const Track& x) { return gone.count (x.id) > 0; });
    // Anything that sent or was routed to them goes to its own default.
    auto unroute = [&] (Track& x)
    {
        eraseIf (x.sends, [&] (const Send& s) { return gone.count (s.target) > 0; });
        if (gone.count (x.output)) x.output = 0;
        for (auto& m : x.modulators) if (gone.count (m.sourceTrack)) m.sourceTrack = 0;
    };
    for (auto& x : p.tracks) unroute (x);
    unroute (p.master);
}

Id duplicateTrack (Project& p, Id id)
{
    const int i = p.indexOf (id);
    if (i < 0) return 0;
    const size_t end = blockEnd (p, static_cast<size_t> (i));
    std::vector<Track> copies (p.tracks.begin() + i, p.tracks.begin() + static_cast<std::ptrdiff_t> (end));
    std::map<Id, Id> ids;
    for (auto& c : copies)
    {
        const Id old = c.id;
        renumber (p, c);
        ids[old] = c.id;
    }
    for (auto& c : copies)
        if (ids.count (c.parent)) c.parent = ids[c.parent];
    copies.front().name += " Copy";
    p.tracks.insert (p.tracks.begin() + static_cast<std::ptrdiff_t> (end), copies.begin(), copies.end());
    return copies.front().id;
}

bool moveTrack (Project& p, Id id, Id before, Id parent)
{
    const int i = p.indexOf (id);
    if (i < 0 || id == before || id == parent) return false;
    if (parent != 0 && (p.isInside (parent, id) || ! p.track (parent) || ! p.track (parent)->isFolder())) return false;
    if (before != 0 && p.isInside (before, id)) return false;

    const size_t end = blockEnd (p, static_cast<size_t> (i));
    std::vector<Track> block (p.tracks.begin() + i, p.tracks.begin() + static_cast<std::ptrdiff_t> (end));
    p.tracks.erase (p.tracks.begin() + i, p.tracks.begin() + static_cast<std::ptrdiff_t> (end));
    block.front().parent = parent;

    size_t at;
    if (before != 0 && p.indexOf (before) >= 0)
        at = static_cast<size_t> (p.indexOf (before));
    else if (parent != 0)
        at = blockEnd (p, static_cast<size_t> (p.indexOf (parent)));
    else
        at = p.tracks.size();

    // Inside a folder, a track has to land within the folder's block.
    if (parent != 0)
    {
        const size_t pi = static_cast<size_t> (p.indexOf (parent));
        at = std::clamp (at, pi + 1, blockEnd (p, pi));
    }
    p.tracks.insert (p.tracks.begin() + static_cast<std::ptrdiff_t> (at), block.begin(), block.end());
    return true;
}

Id wrapInFolder (Project& p, const std::vector<Id>& ids, const std::string& name)
{
    if (ids.empty()) return 0;
    const auto* first = p.track (ids.front());
    if (! first) return 0;
    const Id parent = first->parent;
    for (auto id : ids)
        if (! p.track (id) || p.track (id)->parent != parent) return 0;

    // The folder goes where the first of them is.
    Id before = ids.front();
    int lowest = p.indexOf (before);
    for (auto id : ids) if (p.indexOf (id) < lowest) { lowest = p.indexOf (id); before = id; }

    Track f;
    f.id = p.newId();
    f.kind = TrackKind::Folder;
    f.name = name;
    f.parent = parent;
    f.height = 40;
    f.versions.front().id = p.newId();
    p.tracks.insert (p.tracks.begin() + lowest, f);

    std::vector<Id> ordered (ids);
    std::sort (ordered.begin(), ordered.end(), [&] (Id a, Id b) { return p.indexOf (a) < p.indexOf (b); });
    for (auto id : ordered) moveTrack (p, id, 0, f.id);
    return f.id;
}

//==============================================================================
// The active version's devices are the track's; the others keep their own
// (decision 0050). These move them between the two.
namespace
{
void stashDevices (Track& t)
{
    auto& v = t.version();
    v.devices = t.devices;
    v.ownDevices = true;
}

void takeDevices (Track& t, int index)
{
    auto& v = t.versions[static_cast<size_t> (index)];
    // A version from before versions had their own devices takes the track's.
    if (v.ownDevices) t.devices = std::move (v.devices);
    v.devices.clear();
    v.ownDevices = true;
    t.activeVersion = index;
}

int addVersion (Project& p, Track& t, TrackVersion v)
{
    stashDevices (t);
    v.id = p.newId();
    v.name = "Version " + std::to_string (t.versions.size() + 1);
    v.devices.clear();
    v.ownDevices = true;   // the track's devices, copied: the same ids, its own settings from here
    t.versions.push_back (std::move (v));
    t.activeVersion = static_cast<int> (t.versions.size()) - 1;
    return t.activeVersion;
}
} // namespace

int newVersion (Project& p, Id id)
{
    auto* t = p.track (id);
    if (! t || ! t->holdsClips() || static_cast<int> (t->versions.size()) >= maxVersions) return -1;
    TrackVersion v = t->version();
    for (auto& c : v.clips)
    {
        c.id = p.newId();
        for (auto& n : c.midi.notes) n.id = p.newId();
    }
    return addVersion (p, *t, std::move (v));
}

int newEmptyVersion (Project& p, Id id)
{
    auto* t = p.track (id);
    if (! t || ! t->holdsClips() || static_cast<int> (t->versions.size()) >= maxVersions) return -1;
    return addVersion (p, *t, {});
}

bool deleteVersion (Project& p, Id id, int index)
{
    auto* t = p.track (id);
    if (! t || t->versions.size() < 2 || index < 0 || index >= static_cast<int> (t->versions.size())) return false;
    const bool wasActive = index == t->activeVersion;
    if (! wasActive) stashDevices (*t);
    t->versions.erase (t->versions.begin() + index);
    int next = t->activeVersion;
    if (next >= index && next > 0) --next;
    if (wasActive) t->devices.clear();
    takeDevices (*t, next);
    return true;
}

void selectVersion (Project& p, Id id, int index)
{
    auto* t = p.track (id);
    if (! t) return;
    index = std::clamp (index, 0, static_cast<int> (t->versions.size()) - 1);
    if (index == t->activeVersion) return;
    stashDevices (*t);
    takeDevices (*t, index);
}

void renameVersion (Project& p, Id id, int index, const std::string& name)
{
    if (auto* t = p.track (id))
        if (index >= 0 && index < static_cast<int> (t->versions.size()))
            t->versions[static_cast<size_t> (index)].name = name;
}

//==============================================================================
// Song versions (decision 0103)

StoredSong storeSong (const Project& p)
{
    Project q = p;
    q.otherVersion = {};
    q.songVersion = 0;
    return { std::make_shared<const std::string> (file::save (q)) };
}

bool newSongVersion (Project& p)
{
    if (p.songVersions() >= maxSongVersions) return false;
    // The copy plays on as version 2, so nothing has to load again; version 1
    // is stored, with an id of its own.
    Project first = p;
    first.songId = p.newId();
    p.otherVersion = storeSong (first);
    p.songVersion = 1;
    return true;
}

bool selectSongVersion (Project& p, int index)
{
    if (index == p.songVersion || index < 0 || index >= p.songVersions()) return false;
    auto other = file::load (*p.otherVersion.json);
    if (! other) return false;
    Project next = std::move (*other);
    // What's the song's, not a version's.
    next.name = p.name;
    next.metadata = p.metadata;
    next.workSeconds = p.workSeconds;
    next.sampleRate = p.sampleRate;
    next.nextId = std::max (next.nextId, p.nextId);
    next.otherVersion = storeSong (p);
    next.songVersion = index;
    p = std::move (next);
    return true;
}

bool deleteSongVersion (Project& p, int index)
{
    if (p.songVersions() < 2 || index < 0 || index >= p.songVersions()) return false;
    if (index == p.songVersion && ! selectSongVersion (p, 1 - index)) return false;
    p.otherVersion = {};
    p.songVersion = 0;
    return true;
}

bool otherVersionUses (const Project& p, const std::string& f)
{
    if (p.otherVersion.empty() || f.empty()) return false;
    const auto other = file::load (*p.otherVersion.json);
    if (! other) return false;
    for (auto& t : other->tracks)
        if (t.frozenFile == f) return true;
    return false;
}

//==============================================================================
void sortClips (Track& t)
{
    std::stable_sort (t.clips().begin(), t.clips().end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
}

Id addClip (Project& p, Id trackId, Clip c)
{
    auto* t = p.track (trackId);
    if (! t || ! t->holdsClips()) return 0;
    c.id = p.newId();
    for (auto& n : c.midi.notes) if (n.id == 0) n.id = p.newId();
    c.length = std::max<Tick> (1, c.length);
    t->clips().push_back (c);
    sortClips (*t);
    return c.id;
}

Id addMidiClip (Project& p, Id trackId, Tick start, Tick length)
{
    Clip c;
    c.kind = Clip::Kind::Midi;
    c.start = start;
    c.length = length;
    if (auto* t = p.track (trackId)) c.name = t->name;
    return addClip (p, trackId, c);
}

void removeClips (Project& p, const std::set<Id>& ids)
{
    for (auto& t : p.tracks)
        eraseIf (t.clips(), [&] (const Clip& c) { return ids.count (c.id) > 0 && ! c.locked; });
}

bool moveClip (Project& p, Id clipId, Tick newStart, Id newTrack)
{
    Track* owner = nullptr;
    auto* c = p.clip (clipId, &owner);
    if (! c || c->locked || owner->locked) return false;
    newStart = std::max<Tick> (0, newStart);
    if (newTrack == 0 || newTrack == owner->id)
    {
        c->start = newStart;
        sortClips (*owner);
        return true;
    }
    auto* target = p.track (newTrack);
    if (! target || ! target->holdsClips() || target->locked) return false;
    Clip moved = *c;
    moved.start = newStart;
    eraseIf (owner->clips(), [clipId] (const Clip& x) { return x.id == clipId; });
    target->clips().push_back (moved);
    sortClips (*target);
    return true;
}

void loopContent (Project& p, Id clipId)
{
    auto* c = p.clip (clipId);
    if (! c || c->locked) return;
    if (c->loopLength > 0) { c->loopLength = 0; return; }
    if (c->isMidi() && c->offset != 0)
    {
        const Tick off = c->offset;
        eraseIf (c->midi.notes, [off] (const Note& n) { return n.start < off; });
        for (auto& n : c->midi.notes) n.start -= off;
        eraseIf (c->midi.events, [off] (const MidiEvent& e) { return e.tick < off; });
        for (auto& e : c->midi.events) e.tick -= off;
        c->offset = 0;
    }
    c->loopLength = c->length + c->offset;
    c->length *= 2;
}

void resizeClip (Project& p, Id clipId, Tick newStart, Tick newEnd)
{
    Track* owner = nullptr;
    auto* c = p.clip (clipId, &owner);
    if (! c || c->locked) return;
    newStart = std::max<Tick> (0, newStart);
    if (newEnd <= newStart) newEnd = newStart + 1;
    // Trimming the front keeps the content where it was in time.
    c->offset += newStart - c->start;
    if (c->offset < 0 && c->loopLength == 0)
    {
        newStart -= c->offset;
        c->offset = 0;
    }
    c->start = newStart;
    c->length = newEnd - newStart;
    sortClips (*owner);
}

Id splitClip (Project& p, Id clipId, Tick at, double fine)
{
    Track* owner = nullptr;
    auto* c = p.clip (clipId, &owner);
    if (! c || c->locked || at <= c->start || at >= c->end()) return 0;
    Clip right = *c;
    right.id = p.newId();
    right.start = at;
    right.length = c->end() - at;
    right.offset = c->offset + (at - c->start);
    c->length = at - c->start;
    if (c->isMidi() && c->loopLength == 0)
    {
        // Each half keeps only the notes that start in it, so editing one never edits the other.
        const Tick cut = c->offset + c->length;
        eraseIf (c->midi.notes, [cut] (const Note& n) { return n.start >= cut; });
        eraseIf (right.midi.notes, [cut] (const Note& n) { return n.start < cut; });
        for (auto& n : right.midi.notes) n.id = p.newId();
    }
    // Fades stay at their own end; both halves meet on the same sample.
    right.audio.fadeIn = 0;
    c->audio.fadeOut = 0;
    c->audio.endFine = fine;
    right.audio.startFine = fine;
    const Id rightId = right.id;
    owner->clips().push_back (right);
    sortClips (*owner);
    return rightId;
}

std::vector<Id> keepSourceSpans (Project& p, Id clipId, const std::vector<std::pair<double, double>>& spans)
{
    Track* owner = nullptr;
    auto* c = p.clip (clipId, &owner);
    if (! c || c->locked || c->isMidi() || c->loopLength > 0) return {};
    const Clip original = *c;
    const Tick contentStart = original.start - original.offset;
    std::vector<Clip> pieces;
    for (auto [from, to] : spans)
    {
        Tick a = audiotools::tickForSource (original.audio, from, p.tempo, contentStart);
        Tick b = audiotools::tickForSource (original.audio, to, p.tempo, contentStart);
        a = std::max (a, original.offset);
        b = std::min (b, original.offset + original.length);
        if (b - a < 2) continue;
        Clip piece = original;
        piece.id = p.newId();
        piece.start = contentStart + a;
        piece.offset = a;
        piece.length = b - a;
        piece.audio.fadeIn = piece.audio.fadeOut = 0.005;
        pieces.push_back (piece);
    }
    eraseIf (owner->clips(), [clipId] (const Clip& x) { return x.id == clipId; });
    std::vector<Id> ids;
    for (auto& piece : pieces) { ids.push_back (piece.id); owner->clips().push_back (piece); }
    sortClips (*owner);
    return ids;
}

std::set<Tick> cutsAtSource (const Project& p, Id clipId, const std::vector<double>& hits)
{
    const auto* c = p.clip (clipId);
    if (! c || c->locked || c->isMidi() || c->loopLength > 0 || c->audio.reversed) return {};
    const Tick contentStart = c->start - c->offset;
    const Tick edge = std::max<Tick> (1, p.tempo.tickAt (p.tempo.secondsAt (c->start) + 0.01) - c->start);
    std::set<Tick> cuts;
    for (double h : hits)
    {
        const Tick at = contentStart + audiotools::tickForSource (c->audio, h, p.tempo, contentStart);
        if (at > c->start + edge && at < c->end() - edge) cuts.insert (at);
    }
    return cuts;
}

std::vector<Id> splitAtSource (Project& p, Id clipId, const std::vector<double>& hits)
{
    auto* c = p.clip (clipId);
    if (! c || c->locked || c->isMidi() || c->loopLength > 0 || c->audio.reversed) return {};
    const auto cuts = cutsAtSource (p, clipId, hits);
    std::vector<Id> ids { clipId };
    // From the last cut back, so the clip keeps its id and is always the piece cut.
    for (auto it = cuts.rbegin(); it != cuts.rend(); ++it)
        if (const Id right = splitClip (p, clipId, *it)) ids.insert (ids.begin() + 1, right);
    return ids;
}

namespace
{
// What a clip plays of its content, in its own ticks: its loop, or the stretch it shows.
std::pair<Tick, Tick> playedWindow (const Clip& c)
{
    if (c.loopLength > 0) return { 0, c.loopLength };
    return { c.offset, c.offset + c.length };
}
} // namespace

void humanise (Project& p, Id clipId, Tick grid, unsigned seed, double amount)
{
    auto* c = midiClip (p, clipId);
    if (! c || c->locked || c->midi.notes.empty()) return;
    std::mt19937 rng (seed);
    std::uniform_real_distribution<double> either (-1.0, 1.0);
    const double reach = std::max<Tick> (1, grid) * amount;
    for (auto& n : c->midi.notes)
    {
        const int v = (int) std::lround (n.velocity * (1.0 + amount * either (rng)));
        n.velocity = std::clamp (v, 1, 127);
        n.start = std::max<Tick> (0, n.start + (Tick) std::llround (reach * either (rng)));
    }
    sortNotes (c->midi);
}

void reverseClip (Project& p, Id clipId)
{
    auto* c = p.clip (clipId);
    if (! c || c->locked) return;
    if (! c->isMidi()) { c->audio.reversed = ! c->audio.reversed; return; }
    const auto [from, to] = playedWindow (*c);
    for (auto& n : c->midi.notes)
        if (n.start >= from && n.start < to)
            n.start = std::max (from, from + to - (n.start + n.length));
    for (auto& e : c->midi.events)
        if (e.tick >= from && e.tick < to) e.tick = std::max (from, from + to - 1 - e.tick);
    std::stable_sort (c->midi.notes.begin(), c->midi.notes.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    std::stable_sort (c->midi.events.begin(), c->midi.events.end(), [] (const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
}

void invertClip (Project& p, Id clipId)
{
    auto* c = p.clip (clipId);
    if (! c || c->locked) return;
    if (! c->isMidi()) { c->audio.inverted = ! c->audio.inverted; return; }
    if (c->midi.notes.empty()) return;
    int lo = 127, hi = 0;
    for (auto& n : c->midi.notes) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }
    for (auto& n : c->midi.notes) n.pitch = std::clamp (lo + hi - n.pitch, 0, 127);
}

Id duplicateClip (Project& p, Id clipId)
{
    Track* owner = nullptr;
    auto* c = p.clip (clipId, &owner);
    if (! c) return 0;
    Clip copy = *c;
    copy.start = c->end();
    copy.locked = false;
    for (auto& n : copy.midi.notes) n.id = p.newId();
    return addClip (p, owner->id, copy);
}

Id consolidateClips (Project& p, const std::set<Id>& ids)
{
    Track* owner = nullptr;
    std::vector<Clip> chosen;
    for (auto id : ids)
    {
        Track* o = nullptr;
        auto* c = p.clip (id, &o);
        if (! c || ! c->isMidi()) return 0;
        if (owner && owner != o) return 0;
        owner = o;
        chosen.push_back (*c);
    }
    if (chosen.empty() || ! owner) return 0;
    Clip out;
    out.kind = Clip::Kind::Midi;
    out.name = chosen.front().name;
    out.colour = chosen.front().colour;
    out.start = chosen.front().start;
    Tick end = 0;
    for (auto& c : chosen) { out.start = std::min (out.start, c.start); end = std::max (end, c.end()); }
    out.length = end - out.start;
    for (auto& c : chosen)
    {
        // Every note the clip actually plays, looped copies included, at its place in time.
        const Tick content = c.loopLength > 0 ? c.loopLength : std::max<Tick> (1, c.length + c.offset);
        for (Tick base = -c.offset; base < c.length; base += content)
        {
            for (auto n : c.midi.notes)
            {
                const Tick at = base + n.start;
                if (at < 0 || at >= c.length) continue;
                n.start = c.start - out.start + at;
                n.length = std::min (n.length, c.length - at);
                n.id = 0;
                out.midi.notes.push_back (n);
            }
            for (auto e : c.midi.events)
            {
                const Tick at = base + e.tick;
                if (at < 0 || at >= c.length) continue;
                e.tick = c.start - out.start + at;
                out.midi.events.push_back (e);
            }
            if (c.loopLength == 0) break;
        }
    }
    sortNotes (out.midi);
    eraseIf (owner->clips(), [&] (const Clip& c) { return ids.count (c.id) > 0; });
    return addClip (p, owner->id, out);
}

//==============================================================================
void sortNotes (MidiData& m)
{
    std::stable_sort (m.notes.begin(), m.notes.end(), [] (const Note& a, const Note& b)
    {
        return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
    });
    std::stable_sort (m.events.begin(), m.events.end(), [] (const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
}

Id addNote (Project& p, Id clipId, Note n)
{
    auto* c = midiClip (p, clipId);
    if (! c) return 0;
    n.id = p.newId();
    n.pitch = std::clamp (n.pitch, 0, 127);
    n.velocity = std::clamp (n.velocity, 1, 127);
    n.length = std::max<Tick> (1, n.length);
    n.start = std::max<Tick> (0, n.start);
    c->midi.notes.push_back (n);
    sortNotes (c->midi);
    return n.id;
}

void removeNotes (Project& p, Id clipId, const std::set<Id>& ids)
{
    if (auto* c = midiClip (p, clipId))
        eraseIf (c->midi.notes, [&] (const Note& n) { return ids.count (n.id) > 0; });
}

void moveNotes (Project& p, Id clipId, const std::set<Id>& ids, Tick dt, int dPitch)
{
    auto* c = midiClip (p, clipId);
    if (! c) return;
    // The group moves as one: it stops at the edges rather than squashing.
    Tick earliest = std::numeric_limits<Tick>::max();
    int lo = 127, hi = 0;
    for (auto& n : c->midi.notes)
        if (ids.count (n.id)) { earliest = std::min (earliest, n.start); lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }
    if (earliest == std::numeric_limits<Tick>::max()) return;
    dt = std::max (dt, -earliest);
    dPitch = std::clamp (dPitch, -lo, 127 - hi);
    for (auto& n : c->midi.notes)
        if (ids.count (n.id)) { n.start += dt; n.pitch += dPitch; }
    sortNotes (c->midi);
}

void resizeNotes (Project& p, Id clipId, const std::set<Id>& ids, Tick dStart, Tick dEnd, Tick minLength)
{
    auto* c = midiClip (p, clipId);
    if (! c) return;
    minLength = std::max<Tick> (1, minLength);
    for (auto& n : c->midi.notes)
    {
        if (! ids.count (n.id)) continue;
        const Tick end = n.end() + dEnd;
        Tick start = std::max<Tick> (0, n.start + dStart);
        if (end - start < minLength)
        {
            if (dStart != 0) start = end - minLength;
            else { n.length = minLength; continue; }
        }
        n.start = std::max<Tick> (0, start);
        n.length = std::max (minLength, end - n.start);
    }
    sortNotes (c->midi);
}

void setVelocity (Project& p, Id clipId, const std::set<Id>& ids, int velocity)
{
    if (auto* c = midiClip (p, clipId))
        for (auto& n : c->midi.notes) if (ids.count (n.id)) n.velocity = std::clamp (velocity, 1, 127);
}

void scaleVelocity (Project& p, Id clipId, const std::set<Id>& ids, double factor)
{
    if (auto* c = midiClip (p, clipId))
        for (auto& n : c->midi.notes)
            if (ids.count (n.id)) n.velocity = std::clamp (static_cast<int> (std::lround (n.velocity * factor)), 1, 127);
}

std::set<Id> duplicateNotes (Project& p, Id clipId, const std::set<Id>& ids, Tick dt)
{
    std::set<Id> made;
    auto* c = midiClip (p, clipId);
    if (! c) return made;
    std::vector<Note> copies;
    for (auto& n : c->midi.notes) if (ids.count (n.id)) copies.push_back (n);
    for (auto n : copies)
    {
        n.start = std::max<Tick> (0, n.start + dt);
        n.id = p.newId();
        made.insert (n.id);
        c->midi.notes.push_back (n);
    }
    sortNotes (c->midi);
    return made;
}

static Tick snapTo (Tick t, Tick grid) { return grid <= 0 ? t : static_cast<Tick> (std::llround (static_cast<double> (t) / grid)) * grid; }

void quantise (Project& p, Id clipId, const std::set<Id>& ids, Tick grid, double strength, bool ends)
{
    auto* c = midiClip (p, clipId);
    if (! c || grid <= 0) return;
    strength = std::clamp (strength, 0.0, 1.0);
    for (auto& n : c->midi.notes)
    {
        if (! ids.empty() && ! ids.count (n.id)) continue;
        // Notes are quantised where they sound, so a clip that starts off the bar still lands on the project's grid.
        const Tick abs = c->start - c->offset + n.start;
        const Tick target = snapTo (abs, grid);
        const Tick shift = static_cast<Tick> (std::llround ((target - abs) * strength));
        const Tick end = n.end() + shift;
        n.start = std::max<Tick> (0, n.start + shift);
        n.length = std::max<Tick> (1, end - n.start);
        if (ends)
        {
            const Tick absEnd = c->start - c->offset + n.end();
            const Tick e = snapTo (absEnd, grid);
            const Tick move = static_cast<Tick> (std::llround ((e - absEnd) * strength));
            n.length = std::max<Tick> (grid / 4 > 0 ? grid / 4 : 1, n.length + move);
        }
    }
    sortNotes (c->midi);
}

void transpose (Project& p, Id clipId, const std::set<Id>& ids, int semitones) { moveNotes (p, clipId, ids, 0, semitones); }

void legato (Project& p, Id clipId, const std::set<Id>& ids)
{
    auto* c = midiClip (p, clipId);
    if (! c) return;
    sortNotes (c->midi);
    auto& notes = c->midi.notes;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        if (! ids.empty() && ! ids.count (notes[i].id)) continue;
        // Up to the next start later than its own (a chord's notes all reach the next chord).
        for (size_t j = i + 1; j < notes.size(); ++j)
        {
            if (! ids.empty() && ! ids.count (notes[j].id)) continue;
            if (notes[j].start > notes[i].start) { notes[i].length = notes[j].start - notes[i].start; break; }
        }
    }
}

void humanise (Project& p, Id clipId, const std::set<Id>& ids, Tick timing, int velocity, unsigned seed)
{
    auto* c = midiClip (p, clipId);
    if (! c) return;
    std::mt19937 rng (seed);
    std::uniform_int_distribution<long long> dt (-timing, timing);
    std::uniform_int_distribution<int> dv (-velocity, velocity);
    for (auto& n : c->midi.notes)
    {
        if (! ids.empty() && ! ids.count (n.id)) continue;
        n.start = std::max<Tick> (0, n.start + (timing > 0 ? dt (rng) : 0));
        n.velocity = std::clamp (n.velocity + (velocity > 0 ? dv (rng) : 0), 1, 127);
    }
    sortNotes (c->midi);
}

std::set<Id> splitNotes (Project& p, Id clipId, const std::set<Id>& ids, Tick at)
{
    std::set<Id> made;
    auto* c = midiClip (p, clipId);
    if (! c) return made;
    std::vector<Note> rights;
    for (auto& n : c->midi.notes)
    {
        if (! ids.count (n.id) || at <= n.start || at >= n.end()) continue;
        Note r = n;
        r.id = p.newId();
        r.start = at;
        r.length = n.end() - at;
        n.length = at - n.start;
        rights.push_back (r);
        made.insert (r.id);
    }
    c->midi.notes.insert (c->midi.notes.end(), rights.begin(), rights.end());
    sortNotes (c->midi);
    return made;
}

void joinNotes (Project& p, Id clipId, const std::set<Id>& ids)
{
    auto* c = midiClip (p, clipId);
    if (! c) return;
    // Same-pitch notes chosen together become one from the first start to the last end.
    std::map<int, std::vector<Note*>> byPitch;
    for (auto& n : c->midi.notes) if (ids.count (n.id)) byPitch[n.pitch].push_back (&n);
    std::set<Id> drop;
    for (auto& [pitch, list] : byPitch)
    {
        if (list.size() < 2) continue;
        Tick start = list.front()->start, end = list.front()->end();
        for (auto* n : list) { start = std::min (start, n->start); end = std::max (end, n->end()); }
        list.front()->start = start;
        list.front()->length = end - start;
        for (size_t i = 1; i < list.size(); ++i) drop.insert (list[i]->id);
    }
    eraseIf (c->midi.notes, [&] (const Note& n) { return drop.count (n.id) > 0; });
    sortNotes (c->midi);
}

void setArticulation (Project& p, Id clipId, const std::set<Id>& ids, int articulation)
{
    if (auto* c = midiClip (p, clipId))
        for (auto& n : c->midi.notes) if (ids.count (n.id)) n.articulation = articulation;
}

//==============================================================================
void normaliseChain (Track& t)
{
    std::stable_sort (t.devices.begin(), t.devices.end(), [] (const Device& a, const Device& b)
    {
        return static_cast<int> (a.role) < static_cast<int> (b.role);
    });
}

Id addDevice (Project& p, Id trackId, const PluginRef& plugin, DeviceRole role, int position)
{
    auto* t = p.track (trackId);
    if (! t) return 0;
    if (t->isCue() && role != DeviceRole::AudioFx) return 0;   // nothing plays into it (decision 0051)
    Device d;
    d.id = p.newId();
    d.plugin = plugin;
    d.role = role;
    if (plugin.format == "Kalibre" && plugin.identifier == "note-splitter") d.midiTo.assign (4, 0);   // four outputs to begin with (0083)
    if (position < 0 || position > static_cast<int> (t->devices.size()))
        t->devices.push_back (d);
    else
        t->devices.insert (t->devices.begin() + position, d);
    normaliseChain (*t);
    return d.id;
}

void removeDevice (Project& p, Id deviceId)
{
    Track* owner = nullptr;
    if (! p.device (deviceId, &owner)) return;
    eraseIf (owner->devices, [deviceId] (const Device& d) { return d.id == deviceId; });
    // Another version may still have it (decision 0050): its lanes stay for that one.
    for (auto& v : owner->versions)
        for (auto& d : v.devices) if (d.id == deviceId) return;
    eraseIf (owner->automation, [deviceId] (const AutomationLane& a) { return a.target.device == deviceId; });
    for (auto& m : owner->modulators)
        eraseIf (m.targets, [deviceId] (const ModTarget& x) { return x.device == deviceId; });
}

void moveDevice (Project& p, Id deviceId, int newIndex)
{
    Track* owner = nullptr;
    auto* d = p.device (deviceId, &owner);
    if (! d) return;
    Device copy = *d;
    eraseIf (owner->devices, [deviceId] (const Device& x) { return x.id == deviceId; });
    newIndex = std::clamp (newIndex, 0, static_cast<int> (owner->devices.size()));
    owner->devices.insert (owner->devices.begin() + newIndex, copy);
    normaliseChain (*owner);
}

void setDeviceActive (Project& p, Id deviceId, bool active, const std::string& state, int latency, double rate)
{
    if (auto* d = p.device (deviceId))
    {
        if (! active && ! state.empty()) d->state = state;
        if (! active) { d->offLatency = std::max (0, latency); d->offLatencyRate = rate; }
        d->active = active;
    }
}

//==============================================================================
bool canRoute (const Project& p, Id from, Id to)
{
    if (from == to || to == 0) return to == 0;
    const auto* a = p.track (from);
    const auto* b = p.track (to);
    if (! a || ! b) return false;
    if (a->kind == TrackKind::Master) return false;
    if (b->kind == TrackKind::Master) return true;
    // `to` must not feed `from`, directly or through folders, outputs or sends.
    std::set<Id> seen;
    std::vector<Id> stack { to };
    while (! stack.empty())
    {
        const Id x = stack.back();
        stack.pop_back();
        if (x == from) return false;
        if (! seen.insert (x).second) continue;
        const auto* t = p.track (x);
        if (! t) continue;
        const Id out = p.outputOf (x);
        if (out != 0) stack.push_back (out);
        for (auto& s : t->sends) stack.push_back (s.target);
    }
    return true;
}

bool addSend (Project& p, Id from, Id to, double gainDb)
{
    auto* t = p.track (from);
    if (! t || ! canRoute (p, from, to) || to == 0) return false;
    for (auto& s : t->sends) if (s.target == to) return false;
    t->sends.push_back ({ to, gainDb, false, false });
    return true;
}

//==============================================================================
AutomationLane& laneFor (Track& t, const AutoTarget& target, double defaultValue)
{
    for (auto& a : t.automation) if (a.target == target) return a;
    AutomationLane lane;
    lane.id = 0;   // the caller gives it an id when it knows the project
    lane.target = target;
    lane.points.push_back ({ 0, defaultValue, 0, false });
    t.automation.push_back (lane);
    return t.automation.back();
}

double valueAt (const AutomationLane& lane, Tick tick, double fallback)
{
    return valueAtTicks (lane, (double) tick, fallback);
}

double valueAtTicks (const AutomationLane& lane, double tick, double fallback)
{
    const auto& pts = lane.points;
    if (pts.empty()) return fallback;
    if (tick <= (double) pts.front().tick) return pts.front().value;
    if (tick >= (double) pts.back().tick) return pts.back().value;
    auto it = std::upper_bound (pts.begin(), pts.end(), tick, [] (double t, const AutoPoint& p) { return t < (double) p.tick; });
    const auto& b = *it;
    const auto& a = *(it - 1);
    if (a.step || b.tick == a.tick) return a.value;
    double f = (tick - (double) a.tick) / static_cast<double> (b.tick - a.tick);
    if (std::abs (a.curve) > 1e-6)
    {
        // A power curve: positive bends it to rise late, negative to rise early.
        const double k = std::pow (2.0, a.curve * 3.0);
        f = std::pow (f, k);
    }
    return a.value + (b.value - a.value) * f;
}

void addPoint (AutomationLane& lane, AutoPoint pt)
{
    pt.value = std::clamp (pt.value, 0.0, 1.0);
    pt.tick = std::max<Tick> (0, pt.tick);
    for (auto& p : lane.points) if (p.tick == pt.tick) { p = pt; return; }
    lane.points.push_back (pt);
    std::stable_sort (lane.points.begin(), lane.points.end(), [] (const AutoPoint& a, const AutoPoint& b) { return a.tick < b.tick; });
}

void removePointsIn (AutomationLane& lane, Tick from, Tick to)
{
    eraseIf (lane.points, [from, to] (const AutoPoint& p) { return p.tick >= from && p.tick <= to; });
}

void writeValue (AutomationLane& lane, Tick from, Tick to, double value)
{
    if (to < from) std::swap (from, to);
    eraseIf (lane.points, [from, to] (const AutoPoint& p) { return p.tick > from && p.tick <= to; });
    addPoint (lane, { to, value, 0, false });
}

void writeTrim (AutomationLane& lane, const AutomationLane& original, Tick from, Tick to, double delta)
{
    if (to < from) std::swap (from, to);
    const double fallback = 0.5;
    // What the original had inside keeps its shape, moved.
    std::vector<AutoPoint> inside;
    for (auto& p : original.points)
        if (p.tick > from && p.tick < to) { auto q = p; q.value = std::clamp (p.value + delta, 0.0, 1.0); inside.push_back (q); }
    eraseIf (lane.points, [from, to] (const AutoPoint& p) { return p.tick >= from && p.tick <= to; });
    addPoint (lane, { from, valueAt (original, from, fallback) + delta });
    for (auto& q : inside) addPoint (lane, q);
    addPoint (lane, { to, valueAt (original, to, fallback) + delta });
}

std::vector<AutoPoint> copyPoints (const AutomationLane& lane, Tick from, Tick to)
{
    std::vector<AutoPoint> out;
    if (to <= from || lane.points.empty()) return out;
    out.push_back ({ 0, valueAt (lane, from, 0.5) });
    for (auto& p : lane.points)
        if (p.tick > from && p.tick < to) { auto q = p; q.tick -= from; out.push_back (q); }
    out.push_back ({ to - from - 1, valueAt (lane, to - 1, 0.5) });
    return out;
}

void pastePoints (AutomationLane& lane, const std::vector<AutoPoint>& points, Tick at, Tick length)
{
    if (points.empty()) return;
    const double after = valueAt (lane, at + length, points.back().value);
    const bool had = ! lane.points.empty();
    eraseIf (lane.points, [at, length] (const AutoPoint& p) { return p.tick >= at && p.tick < at + length; });
    for (auto p : points) { p.tick += at; addPoint (lane, p); }
    // What follows carries on as it was.
    if (had) addPoint (lane, { at + length, after });
}

//==============================================================================
void History::push (const Project& before, std::string label)
{
    undoStack.push_back ({ before, std::move (label) });
    if (undoStack.size() > limit) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

bool History::undo (Project& current)
{
    if (undoStack.empty()) return false;
    redoStack.push_back ({ current, undoStack.back().label });
    current = std::move (undoStack.back().project);
    undoStack.pop_back();
    return true;
}

bool History::redo (Project& current)
{
    if (redoStack.empty()) return false;
    undoStack.push_back ({ current, redoStack.back().label });
    current = std::move (redoStack.back().project);
    redoStack.pop_back();
    return true;
}

} // namespace kal::edit
