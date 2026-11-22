#include "Model.h"

namespace kal
{

Project::Project()
{
    master.id = newId();
    master.name = "Master";
    master.kind = TrackKind::Master;
    master.colour = 0xffDDE1E7;
}

Track* Project::track (Id id)
{
    if (id == 0) return nullptr;
    if (master.id == id) return &master;
    for (auto& t : cue) if (t.id == id) return &t;
    for (auto& t : tracks) if (t.id == id) return &t;
    return nullptr;
}

const Track* Project::track (Id id) const { return const_cast<Project*> (this)->track (id); }

int Project::indexOf (Id id) const
{
    for (size_t i = 0; i < tracks.size(); ++i) if (tracks[i].id == id) return static_cast<int> (i);
    return -1;
}

Clip* Project::clip (Id clipId, Track** owner)
{
    for (auto& t : tracks)
        for (auto& v : t.versions)
            for (auto& c : v.clips)
                if (c.id == clipId) { if (owner) *owner = &t; return &c; }
    return nullptr;
}

const Clip* Project::clip (Id clipId, const Track** owner) const
{
    Track* o = nullptr;
    auto* c = const_cast<Project*> (this)->clip (clipId, &o);
    if (owner) *owner = o;
    return c;
}

Device* Project::device (Id deviceId, Track** owner)
{
    auto find = [&] (Track& t) -> Device*
    {
        for (auto& d : t.devices) if (d.id == deviceId) { if (owner) *owner = &t; return &d; }
        return nullptr;
    };
    if (auto* d = find (master)) return d;
    for (auto& t : cue) if (auto* d = find (t)) return d;
    for (auto& t : tracks) if (auto* d = find (t)) return d;
    return nullptr;
}

const ArticulationMap* Project::articulationMap (Id id) const
{
    for (auto& m : articulationMaps) if (m.id == id) return &m;
    return nullptr;
}

int Project::depth (Id id) const
{
    int d = 0;
    const auto* t = track (id);
    while (t && t->parent != 0 && d < 1000) { t = track (t->parent); ++d; }
    return d;
}

bool Project::isInside (Id id, Id folder) const
{
    const auto* t = track (id);
    int guard = 0;
    while (t && t->parent != 0 && guard++ < 1000)
    {
        if (t->parent == folder) return true;
        t = track (t->parent);
    }
    return false;
}

std::vector<Id> Project::childrenOf (Id folder) const
{
    std::vector<Id> out;
    for (auto& t : tracks) if (t.parent == folder) out.push_back (t.id);
    return out;
}

bool Project::hiddenByFolder (Id id) const
{
    const auto* t = track (id);
    int guard = 0;
    while (t && t->parent != 0 && guard++ < 1000)
    {
        t = track (t->parent);
        if (t && t->collapsed) return true;
    }
    return false;
}

Id Project::outputOf (Id id) const
{
    const auto* t = track (id);
    if (! t || t->kind == TrackKind::Master || t->isCue()) return 0;
    if (t->output != 0 && t->output != id && track (t->output) && ! isInside (t->output, id))
        return t->output;
    return t->parent;
}

Tick Project::lastClipEnd() const
{
    Tick end = 0;
    for (auto& t : tracks)
        for (auto& c : t.clips()) end = std::max (end, c.end());
    return end;
}

Tick Project::songEnd() const
{
    const Tick end = lastClipEnd();
    if (end <= 0) return tempo.barStart (1);
    const int bar = tempo.barAt (end - 1);
    return tempo.barStart (bar + 1);
}

Tick Project::endTick() const
{
    Tick end = 0;
    for (auto& t : tracks)
        for (auto& c : t.clips()) end = std::max (end, c.end());
    const int bars = std::max (16, tempo.barAt (std::max<Tick> (0, end - 1)) + 1);
    return tempo.barStart (bars);
}

} // namespace kal
