#include "NoteSelect.h"

#include "Edit.h"

#include <algorithm>
#include <map>

namespace kal::noteselect
{

std::vector<std::vector<size_t>> chords (const MidiData& m, Tick together)
{
    std::vector<size_t> order (m.notes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort (order.begin(), order.end(), [&] (size_t a, size_t b) { return m.notes[a].start < m.notes[b].start; });
    std::vector<std::vector<size_t>> out;
    Tick groupStart = 0;
    for (auto i : order)
    {
        if (out.empty() || m.notes[i].start - groupStart > together)
        {
            out.push_back ({});
            groupStart = m.notes[i].start;
        }
        out.back().push_back (i);
    }
    return out;
}

static bool has (const std::set<int>& pcs, int root, std::initializer_list<int> intervals)
{
    for (int i : intervals) if (pcs.count ((root + i) % 12)) return true;
    return false;
}

int rootOf (const std::vector<int>& pitches)
{
    std::set<int> pcs;
    int bass = 128;
    for (int p : pitches) { pcs.insert (((p % 12) + 12) % 12); bass = std::min (bass, p); }
    if (pcs.size() < 2) return -1;
    int best = -1, bestScore = -1000;
    for (int r : pcs)
    {
        // Stacked in thirds from it: a third, then a fifth, then a seventh.
        int score = 0;
        if (has (pcs, r, { 3, 4 })) score += 4;
        if (has (pcs, r, { 7 })) score += 3;
        else if (has (pcs, r, { 6, 8 })) score += 1;
        if (has (pcs, r, { 10, 11 })) score += 1;
        // A note a semitone above it is unusual (a b9).
        if (has (pcs, r, { 1 })) score -= 2;
        if (r == ((bass % 12) + 12) % 12) score += 1;   // the bass, on a tie
        if (score > bestScore) { bestScore = score; best = r; }
    }
    return best;
}

int degreeOf (int pitch, int root, const std::vector<int>& chord)
{
    std::set<int> pcs;
    for (int p : chord) pcs.insert (((p % 12) + 12) % 12);
    const int i = (((pitch % 12) + 12) % 12 - root + 12) % 12;
    const bool major3 = pcs.count ((root + 4) % 12), minor3 = pcs.count ((root + 3) % 12);
    const bool fifth = pcs.count ((root + 7) % 12);
    const bool dimTriad = minor3 && ! fifth && pcs.count ((root + 6) % 12);
    const bool seventh = pcs.count ((root + 10) % 12) || pcs.count ((root + 11) % 12);
    switch (i)
    {
        case 0:  return 1;
        case 1:  return 9;                                   // b9
        case 2:  return 9;
        case 3:  return major3 ? 9 : 3;                      // #9 beside a major third
        case 4:  return 3;
        case 5:  return 11;
        case 6:  return fifth ? 11 : 5;                      // #11, or a flat fifth
        case 7:  return 5;
        case 8:  return fifth ? 13 : 5;                      // b13, or a sharp fifth
        case 9:  return dimTriad && ! seventh ? 7 : 13;      // a diminished seventh, or a 13th
        case 10: case 11: return 7;
        default: return 0;
    }
}

static int degreeWanted (What w)
{
    switch (w)
    {
        case What::Root: return 1;
        case What::Third: return 3;
        case What::Fifth: return 5;
        case What::Seventh: return 7;
        case What::Ninth: return 9;
        case What::Eleventh: return 11;
        case What::Thirteenth: return 13;
        default: return 0;
    }
}

std::set<Id> choose (const Clip& clip, const TempoMap& tempo, What what, std::optional<NoteFrom> only)
{
    std::set<Id> out;
    const auto& notes = clip.midi.notes;
    auto counts = [&] (const Note& n) { return ! only || n.from == *only; };
    const Tick base = clip.start - clip.offset;

    if (what == What::OnBeat || what == What::OffBeat)
    {
        const Tick slack = PPQ / 64;   // played in a hair early or late still counts
        for (auto& n : notes)
        {
            if (! counts (n)) continue;
            const Tick at = base + n.start;
            const auto& m = tempo.meterAt (at);
            const Tick beat = m.beatTicks();
            const Tick into = (at - tempo.barStart (tempo.barAt (at))) % std::max<Tick> (1, beat);
            const bool on = into <= slack || beat - into <= slack;
            if (on == (what == What::OnBeat)) out.insert (n.id);
        }
        return out;
    }
    if (what == What::Bottom || what == What::Top)
    {
        // The line along the bottom (or top): a note nothing lower (higher) sounds under (over) as it starts.
        for (auto& n : notes)
        {
            if (! counts (n)) continue;
            bool edge = true;
            for (auto& o : notes)
            {
                if (&o == &n || ! counts (o)) continue;
                const bool sounding = o.start <= n.start && o.end() > n.start;
                if (! sounding) continue;
                if (what == What::Bottom ? o.pitch < n.pitch : o.pitch > n.pitch) { edge = false; break; }
                if (o.pitch == n.pitch && o.start < n.start) { edge = false; break; }
            }
            if (edge) out.insert (n.id);
        }
        return out;
    }
    const int wanted = degreeWanted (what);
    for (auto& group : chords (clip.midi))
    {
        std::vector<int> pitches;
        for (auto i : group) if (counts (notes[i])) pitches.push_back (notes[i].pitch);
        const int root = rootOf (pitches);
        if (root < 0) continue;
        for (auto i : group)
            if (counts (notes[i]) && degreeOf (notes[i].pitch, root, pitches) == wanted) out.insert (notes[i].id);
    }
    return out;
}

std::vector<Id> explode (Project& p, Id clipId)
{
    Track* owner = nullptr;
    const auto* found = p.clip (clipId, &owner);
    if (! found || ! found->isMidi() || ! owner) return {};
    const Clip clip = *found;
    const Id trackId = owner->id;
    // Each note's voice: its place from the bottom of its chord.
    std::vector<int> voice (clip.midi.notes.size(), 0);
    int voices = 0;
    for (auto& group : chords (clip.midi))
    {
        auto sorted = group;
        std::sort (sorted.begin(), sorted.end(), [&] (size_t a, size_t b) { return clip.midi.notes[a].pitch < clip.midi.notes[b].pitch; });
        for (size_t k = 0; k < sorted.size(); ++k) voice[sorted[k]] = (int) k;
        voices = std::max (voices, (int) sorted.size());
    }
    if (voices < 2) return {};

    std::vector<Id> made;
    const std::string base = owner->name;
    Id after = trackId;
    // Made top voice first, each just after the last, so they're listed top to bottom.
    for (int v = voices - 1; v >= 0; --v)
    {
        const Id t = edit::duplicateTrack (p, after);
        auto* nt = p.track (t);
        if (! nt) continue;
        for (auto& ver : nt->versions) { ver.clips.clear(); ver.takes.clear(); ver.comp.clear(); }
        nt->name = base + " " + std::to_string (v + 1) + (v == voices - 1 ? " (Top)" : v == 0 ? " (Bottom)" : "");
        nt->armed = false;
        Clip c = clip;
        c.link = 0;
        c.detectChords = false;
        c.midi.notes.clear();
        for (size_t i = 0; i < clip.midi.notes.size(); ++i)
            if (voice[i] == v) { Note n = clip.midi.notes[i]; n.id = 0; c.midi.notes.push_back (n); }
        c.name = nt->name;
        edit::addClip (p, t, c);
        made.push_back (t);
        after = t;
    }
    edit::removeClips (p, { clipId });
    return made;
}

} // namespace kal::noteselect
