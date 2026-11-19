/*
    Song - edits to the song's shape, across every track (decision 0037).

      - Time: insert empty bars, remove a stretch (closing the gap),
        duplicate or move a stretch. Clips crossing an edge are split there;
        automation, markers, sections and tempo changes move with it.
      - Sections: named stretches (Verse, Chorus) that are moved, duplicated
        and deleted as wholes, through the time edits.
      - Ripple: deleting clips pulls later ones on the same track back by
        the deleted length, so nothing is left with a hole.
      - Linked clips: a duplicate that stays the same as its original; an
        edit to one's notes or audio is made to every clip linked with it.
      - Track groups: tracks in one group move together; a volume or pan
        change keeps their differences, mute, solo and arm are shared.

    No JUCE in here.
*/

#pragma once

#include "Model.h"

#include <set>

namespace kal::song
{

//==============================================================================
// Time
void insertTime (Project&, Tick at, Tick length);
void removeTime (Project&, Tick from, Tick to);
// A copy of [from, to) put in just after it; returns where the copy starts.
Tick duplicateTime (Project&, Tick from, Tick to);
// [from, to) cut out and put back in at `dest` (a tick in the song as it was).
void moveTime (Project&, Tick from, Tick to, Tick dest);

//==============================================================================
// Sections
Id addSection (Project&, const std::string& name, Tick start, Tick end);
void removeSection (Project&, Id, bool withContents);   // with: its bars go too
Id duplicateSection (Project&, Id);
// Moves a section (and everything in it) so it starts where `beforeSection`
// starts; 0 puts it after the last one.
void moveSection (Project&, Id section, Id beforeSection);
// Moves a section (and everything in it) so it starts at `start` (Shift-drag
// in the Sections lane); what was there moves out of its way.
void moveSectionTo (Project&, Id section, Tick start);
void sortSections (Project&);
const Section* sectionAt (const Project&, Tick);

//==============================================================================
// Ripple
void rippleRemove (Project&, const std::set<Id>& clips);

//==============================================================================
// Links
Id duplicateLinked (Project&, Id clip);
void unlink (Project&, Id clip);
// Makes each clip an alias (decision 0074): linked with itself for now, so its
// duplicates are linked with it. Clips already aliases are left as they are.
void makeAlias (Project&, const std::set<Id>& clips);
// The loop range set to span the clips, from the first one's start to the last one's end, and turned on (0071).
bool loopToClips (Project&, const std::set<Id>& clips);
// Show the clips' chords in the chord lane, or stop (0071).
void detectChords (Project&, const std::set<Id>& clips, bool on);
// After an edit: a linked clip whose content changed passes the change on.
void propagateLinks (const Project& before, Project& after);

//==============================================================================
// Groups
struct TrackMix
{
    Id id = 0;
    int group = 0;
    double volumeDb = 0, pan = 0;
    bool mute = false, solo = false, armed = false;
    ArmFor armFor = ArmFor::Audio;
};
std::vector<TrackMix> mixOf (const Project&);
void propagateGroups (const std::vector<TrackMix>& before, Project& after);

} // namespace kal::song
