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
      - The clip launcher: slots of clips, one row a scene, each track
        playing a slot in a loop from the next bar instead of its arrangement.

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
void sortSections (Project&);
const Section* sectionAt (const Project&, Tick);

//==============================================================================
// Ripple
void rippleRemove (Project&, const std::set<Id>& clips);

//==============================================================================
// Links
Id duplicateLinked (Project&, Id clip);
void unlink (Project&, Id clip);
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
};
std::vector<TrackMix> mixOf (const Project&);
void propagateGroups (const std::vector<TrackMix>& before, Project& after);

//==============================================================================
// The launcher
int sceneCount (const Project&);
void setSlot (Project&, Id track, int slot, Clip);
void clearSlot (Project&, Id track, int slot);
// Every slot from the arrangement's clips in [from, to): one scene.
int captureScene (Project&, Tick from, Tick to, const std::string& name);
// Starts a slot at the next bar from `now`; -1 stops the track there.
void launch (Project&, Id track, int slot, Tick now);
void launchScene (Project&, int scene, Tick now);
void backToArrangement (Project&);
bool anyLaunched (const Project&);

} // namespace kal::song
