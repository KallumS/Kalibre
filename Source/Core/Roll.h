/*
    Roll - the piano roll's mouse rules, as a state machine with no drawing.

    The rules (decision 0009):

      - Left click in empty space adds a note (keep holding and drag to make it
        longer).
      - Left click on a note's body and drag moves it - and every other
        chosen note with it. Hold Option to drag a copy.
      - Left click on a note's edge and drag stretches or shortens it, from
        whichever end was grabbed.
      - Right click on a note removes it.
      - Right click and drag over an area removes every note in the area.
      - Shift and left drag chooses every note in the area; with Option too,
        it takes the area's notes out of the choice.
      - Choosing is as everywhere else (decision 0053, asked for 17 November):
        Shift-click on a note chooses every note from the last one clicked to
        it (the pitches and the time between them); Option-click adds it;
        Shift-Option-click takes it out.
      - Hold Command while dragging to move off the grid.

    The window turns the mouse into ticks and pitches and asks the gesture
    what to show while dragging (preview) and what to do once let go
    (release). Nothing touches the project until release, so a drag is one
    undo step (decision 0008).

    No JUCE in here.
*/

#pragma once

#include "Model.h"

#include <functional>
#include <set>

namespace kal::roll
{

enum class Zone { Empty, Body, StartEdge, EndEdge };

// Which part of a note is under the pointer. An edge is `edgeTicks` wide at
// each end, but never more than a third of the note, so a short note can
// still be grabbed in the middle to move it.
Zone zoneOf (const Note&, Tick tick, int pitch, Tick edgeTicks);

// The topmost note under the pointer (the last drawn, so the latest start).
const Note* noteAt (const std::vector<Note>&, Tick tick, int pitch, Tick edgeTicks, Zone* zone = nullptr);

// Notes touching an area (ticks [from, to), pitches lo..hi inclusive).
std::set<Id> notesIn (const std::vector<Note>&, Tick from, Tick to, int lo, int hi);

struct Pointer
{
    Tick tick = 0;
    int pitch = 60;
    bool right = false;    // the right button (or Control-click)
    bool shift = false;
    bool option = false;   // Alt
    bool command = false;
};

struct Settings
{
    Tick edgeTicks = 60;                       // from the edge's width in points at the current zoom
    Tick defaultLength = PPQ / 4;
    int defaultVelocity = 100;
    std::function<Tick (Tick)> snap;           // to the nearest grid line, counted from the bar line; null for none
    std::function<Tick (Tick)> snapDown;       // to the grid line at or before: where a click puts a note
    Tick minLength = PPQ / 4;                  // the grid step: notes never get shorter
};

enum class Action { None, AddNote, RemoveNotes, MoveNotes, CopyNotes, ResizeNotes, Select };

struct Outcome
{
    Action action = Action::None;
    Note added;                        // AddNote: the note, without an id
    std::set<Id> notes;                // the notes acted on
    Tick dt = 0;                       // MoveNotes / CopyNotes
    int dPitch = 0;
    Tick dStart = 0, dEnd = 0;         // ResizeNotes
    std::set<Id> selection;            // what is chosen afterwards
    bool selectionChanged = false;
    bool audition = false;             // play the note that was clicked or added
    int auditionPitch = -1;

    // An area being dragged out, to draw (Select or RemoveNotes).
    bool showArea = false;
    Tick areaFrom = 0, areaTo = 0;
    int areaLo = 0, areaHi = 0;
};

class Gesture
{
public:
    enum class Mode { Idle, Adding, Moving, ResizingStart, ResizingEnd, Selecting, Erasing, RightPending };

    // `anchor`: the note last clicked without Shift, where a Shift-click range starts.
    Gesture (const std::vector<Note>& notes, std::set<Id> selection, Settings, Id anchor = 0);

    Outcome press (const Pointer&);
    Outcome drag (const Pointer&);     // call once the mouse has moved past the drag threshold
    Outcome release (const Pointer&);

    Mode mode() const { return current; }
    const std::set<Id>& selection() const { return chosen; }
    Id anchor() const { return anchorNote; }

private:
    const std::vector<Note>& notes;
    std::set<Id> chosen;
    Settings settings;
    Mode current = Mode::Idle;
    Pointer down;
    Note grabbed;
    bool dragged = false;
    bool copying = false;
    bool subtracting = false;   // Shift-Option: an area or a click takes notes out
    Id anchorNote = 0;
    Outcome last;

    Tick snapped (Tick t, bool free) const;
    Outcome areaOutcome (const Pointer&, Action) const;
};

} // namespace kal::roll
