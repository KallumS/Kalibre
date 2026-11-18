/*
    Edit - every change to a project, as a function.

    The window never changes the model directly: it calls one of these, once
    per gesture, so a drag is one undo step (decision 0008). They keep the
    model's rules - folders followed by what is inside them, clips and notes
    in order, a track's devices MIDI effects first and audio effects last.

    No JUCE in here.
*/

#pragma once

#include "Model.h"

#include <functional>
#include <set>
#include <string>

namespace kal::edit
{

//==============================================================================
// Tracks

// Adds a track after `after` (0: at the end), inside `parent` (0: top level).
Id addTrack (Project&, TrackKind, const std::string& name, Id after = 0, Id parent = 0);
// The Cue track (decision 0051): there's one at most; returns it if it's there already.
Id addCue (Project&);

// Removes a track. A folder takes everything inside it with it unless
// `keepContents`, when its tracks move up to the folder's own parent.
void removeTrack (Project&, Id, bool keepContents = false);

// A copy just after it, with new ids throughout; a folder brings its contents.
Id duplicateTrack (Project&, Id);

// Moves a track (and everything inside it) to just before `before` (0: the
// end), inside `parent`. A folder can't be moved into itself.
bool moveTrack (Project&, Id track, Id before, Id parent);

// Puts the tracks in a new folder, at the first one's place. They must share a parent.
Id wrapInFolder (Project&, const std::vector<Id>& tracks, const std::string& name);

// All the tracks below a folder, at any depth, in order.
std::vector<Id> descendants (const Project&, Id folder);

//==============================================================================
// Versions (decision 0006)

// Four at most (maxVersions); each has its own devices (decision 0050): a new
// one starts with copies of the active one's, under the same ids.
// Copies the active version into a new one and makes it active. Returns its index, or -1.
int newVersion (Project&, Id track);
// An empty new version, made active.
int newEmptyVersion (Project&, Id track);
bool deleteVersion (Project&, Id track, int index);   // the last one stays
void selectVersion (Project&, Id track, int index);
void renameVersion (Project&, Id track, int index, const std::string&);

//==============================================================================
// Clips

Id addClip (Project&, Id track, Clip);
Id addMidiClip (Project&, Id track, Tick start, Tick length);
void removeClips (Project&, const std::set<Id>& clips);
bool moveClip (Project&, Id clip, Tick newStart, Id newTrack = 0);
void resizeClip (Project&, Id clip, Tick newStart, Tick newEnd);   // trims from either side
// Returns the right half's id. `fine`: for audio, how far past `at` (seconds,
// under a tick) the cut really is, so it can be on a zero crossing (decision 0054).
Id splitClip (Project&, Id clip, Tick at, double fine = 0);
Id duplicateClip (Project&, Id clip);                              // just after itself
Id consolidateClips (Project&, const std::set<Id>& clips);         // MIDI clips on one track into one
// Loop Its Content, or let it go: the clip doubles and repeats what it plays.
// A MIDI clip's content is first moved to start where the clip does (its
// offset made 0), so the loop is just what it played (a split's right half
// looped silence before, and the editors wrote past the loop's end).
void loopContent (Project&, Id clip);
void sortClips (Track&);
// Strip Silence (decision 0036): an audio clip becomes one clip for each
// stretch of its file (seconds) that has sound, each in its place, with short fades.
std::vector<Id> keepSourceSpans (Project&, Id clip, const std::vector<std::pair<double, double>>& sourceSpans);

//==============================================================================
// Notes (in a MIDI clip; ticks are the clip's own, from its content start)

Id addNote (Project&, Id clip, Note);
void removeNotes (Project&, Id clip, const std::set<Id>& notes);
void moveNotes (Project&, Id clip, const std::set<Id>& notes, Tick dt, int dPitch);
void resizeNotes (Project&, Id clip, const std::set<Id>& notes, Tick dStart, Tick dEnd, Tick minLength);
void setVelocity (Project&, Id clip, const std::set<Id>& notes, int velocity);
void scaleVelocity (Project&, Id clip, const std::set<Id>& notes, double factor);
std::set<Id> duplicateNotes (Project&, Id clip, const std::set<Id>& notes, Tick dt);
void quantise (Project&, Id clip, const std::set<Id>& notes, Tick grid, double strength = 1.0, bool ends = false);
void transpose (Project&, Id clip, const std::set<Id>& notes, int semitones);
void legato (Project&, Id clip, const std::set<Id>& notes);
void humanise (Project&, Id clip, const std::set<Id>& notes, Tick timing, int velocity, unsigned seed);
std::set<Id> splitNotes (Project&, Id clip, const std::set<Id>& notes, Tick at);
void joinNotes (Project&, Id clip, const std::set<Id>& notes);
void setArticulation (Project&, Id clip, const std::set<Id>& notes, int articulation);
void sortNotes (MidiData&);

//==============================================================================
// Devices (decision 0007)

Id addDevice (Project&, Id track, const PluginRef&, DeviceRole, int position = -1);
void removeDevice (Project&, Id device);
void moveDevice (Project&, Id device, int newIndex);
// Switched off, its latency (samples at `rate`) is kept, so a delay can stand in for it (decision 0086).
void setDeviceActive (Project&, Id device, bool active, const std::string& stateIfUnloading = {}, int latency = 0, double rate = 0);
void normaliseChain (Track&);

//==============================================================================
// Routing

// Whether `from` may send to `to` without the sound coming back round to itself.
bool canRoute (const Project&, Id from, Id to);
bool addSend (Project&, Id from, Id to, double gainDb = -6.0);

//==============================================================================
// Automation

AutomationLane& laneFor (Track&, const AutoTarget&, double defaultValue);
double valueAt (const AutomationLane&, Tick, double fallback);
// Between ticks too: the audio thread follows a curve sample by sample.
double valueAtTicks (const AutomationLane&, double ticks, double fallback);
void addPoint (AutomationLane&, AutoPoint);
void removePointsIn (AutomationLane&, Tick from, Tick to);
// Writes a value at a tick as a performer would: Touch and Latch replace what
// was there from the last write to here.
void writeValue (AutomationLane&, Tick from, Tick to, double value);
// Trim mode: over [from, to] the lane becomes what it was (`original`) moved by `delta`.
void writeTrim (AutomationLane&, const AutomationLane& original, Tick from, Tick to, double delta);
// Copy and paste: the points of [from, to), from `from`, with its two edges' values.
std::vector<AutoPoint> copyPoints (const AutomationLane&, Tick from, Tick to);
// Replaces [at, at + length) with them.
void pastePoints (AutomationLane&, const std::vector<AutoPoint>&, Tick at, Tick length);

//==============================================================================
// Undo (snapshots; the project is small without its audio)

class History
{
public:
    void push (const Project& before, std::string label);
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    std::string undoLabel() const { return canUndo() ? undoStack.back().label : std::string {}; }
    std::string redoLabel() const { return canRedo() ? redoStack.back().label : std::string {}; }
    bool undo (Project& current);
    bool redo (Project& current);
    void clear() { undoStack.clear(); redoStack.clear(); }
    size_t size() const { return undoStack.size(); }

    static constexpr size_t limit = 500;

private:
    struct Entry { Project project; std::string label; };
    std::vector<Entry> undoStack, redoStack;
};

} // namespace kal::edit
