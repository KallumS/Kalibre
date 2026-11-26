/*
    Toolbox - the tools a left click can be, chosen from the toolbox that
    Control-right-click opens over the arrangement or the piano roll
    (decision 0092). Adapted from the family's Right-Click Toolbox for
    REAPER, which gave REAPER Cubase's toolbox.

    Each area keeps its own tool, and each tool the mode it was last used
    in. Choosing a tool isn't an undo step; what a tool does is one.

    Here too, the edits the tools make that aren't plain edit:: calls:
    cutting into repeated pieces, gluing a clip to the next, clearing a
    range, the shapes the Line tool draws, and Time Warp's tempo.

    No JUCE in here.
*/

#pragma once

#include "Model.h"

#include <array>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace kal::tools
{

enum class Tool { Select, Range, Split, Glue, Erase, Zoom, Mute, Draw, Line, Play, Hand, Drumstick, TimeWarp };
inline constexpr int toolCount = 13;

enum class Area { Arrange, Roll };

struct Mode
{
    const char* name;   // Title Case, for the menu
    std::string help;   // in the words of the system it runs on (Option-click, Alt+click: 0129)
};

struct Info
{
    Tool tool;
    const char* name;   // "Time Warp"
    std::string help;   // what a click does, in a sentence or two
    bool inArrange, inRoll;
    std::vector<Mode> modes;   // empty: the tool has one way of working
};

const std::vector<Info>& all();          // in the toolbox's order
const Info& info (Tool);
bool availableIn (Tool, Area);

// The modes, by number, where a tool's code asks.
namespace selectmode { enum { Normal, ClickAddsOrRemoves, EverythingAtClick }; }
namespace splitmode  { enum { Clicked, EveryChosen }; }
namespace zoommode   { enum { DraggedArea, Horizontally, Vertically }; }
namespace playmode   { enum { WhileHeld, FromClick }; }

// The Line tool's shapes: its modes, in order.
enum class Shape { Linear, Curve, SCurve, FastStart, FastEnd, Steps };

// One area's tool, and the mode each tool was last used in.
struct State
{
    Tool tool = Tool::Select;
    std::array<int, toolCount> modes {};

    int mode() const { return modeOf (tool); }
    int modeOf (Tool t) const { return modes[(size_t) t]; }
    // Picks the tool, in `mode` if given (otherwise as it was last used).
    // False when the tool isn't one for this area.
    bool choose (Tool, Area, std::optional<int> mode = std::nullopt);
    std::string describe() const;   // "Split (Split Every Chosen One)", for the message line
};

//==============================================================================
// Split, Option-click: where to cut so the thing is pieces as long as from
// its start to the click. Each cut is inside (start, end).
std::vector<Tick> repeatedCuts (Tick start, Tick end, Tick at);

// Splits the clip at every cut (from the right, so the ids stay with the
// pieces they started as). Returns the pieces made.
std::vector<Id> splitClipAt (Project&, Id clip, const std::vector<Tick>& cuts);
// The same for one note: the pieces made.
std::set<Id> splitNoteAt (Project&, Id clip, Id note, const std::vector<Tick>& cuts);

//==============================================================================
// Glue: the clip joined to the next one on its track.
struct Glued
{
    Id clip = 0;                 // what's left, or 0
    Id next = 0;                 // the clip after it, whether or not it was glued
    bool needsRender = false;    // two different recordings: only Consolidate (a new file) can join them
    bool mixed = false;          // one MIDI and one audio: they can't be joined
};
// The clip after `clip` on its track: the first to start after it starts.
Id nextClip (const Project&, Id clip);
// MIDI clips become one (edit::consolidateClips); two pieces of the same
// recording that meet are joined back into one. Otherwise nothing changes.
Glued glueToNext (Project&, Id clip);
// Join (decision 0141): clips on one track glued into one, each to the next,
// without a new file. The new clip's id, or 0 when they can't be (a MIDI
// clip with an audio one, a locked one, another clip between them, audio of
// different recordings): then `p` is left as it was.
Id glueAll (Project&, const std::set<Id>& clips);
// The next note of the same pitch, joined to this one.
Id glueNote (Project&, Id clip, Id note);

//==============================================================================
// Range: what's in [from, to) on these tracks is deleted, clips cut at its
// edges. Locked clips are left. Returns how many clips were touched.
int clearRange (Project&, const std::vector<Id>& tracks, Tick from, Tick to);

// Select, "Everything at the Click": every clip the tick falls in, on every
// track, and every note in a clip sounding at it.
std::set<Id> clipsAt (const Project&, Tick);
std::set<Id> notesAt (const std::vector<Note>&, Tick);

//==============================================================================
// Line: f(0) = 0, f(1) = 1, the shape's way between.
double shaped (Shape, double x);
// Automation points for a line from (a, va) to (b, vb): two points, or more for
// the S-curve, or a step at every `step` ticks for Steps. Curves are made with
// the points' own bend, so they can be bent again afterwards.
std::vector<AutoPoint> linePoints (Tick a, double va, Tick b, double vb, Shape, Tick step);
// The Line tool in the piano roll: a note every `step` from a to b, the pitch
// moving from one end's to the other's by the shape. Each `step` long.
std::vector<Note> lineNotes (Tick a, int pitchA, Tick b, int pitchB, Shape, Tick step, int velocity);

//==============================================================================
// Time Warp: the beat at `beat` moved to `toSeconds` by changing the tempo
// from the tempo change before it, so everything after moves with it and
// everything before stays. A tempo change is put at `beat` first, if there
// isn't one, so the tempo after it stays as it was. False if it can't be done
// (the beat would land before the change before it, or out of tempo range).
bool warpBeat (TempoMap&, Tick beat, double toSeconds);
// The same for the song: audio playing at its own speed (warping off) stays
// where it is in time, so the grid moves under it to line up; MIDI and warped
// audio follow the beats.
bool warpBeat (Project&, Tick beat, double toSeconds);

} // namespace kal::tools
