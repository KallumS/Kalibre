#include "Check.h"
#include "Edit.h"

using namespace kal;

TEST ("folders: tracks nest and move with their folder")
{
    Project p;
    const Id a = edit::addTrack (p, TrackKind::Track, "A");
    const Id f = edit::addTrack (p, TrackKind::Folder, "Strings");
    const Id v1 = edit::addTrack (p, TrackKind::Track, "Violin 1", 0, f);
    const Id inner = edit::addTrack (p, TrackKind::Folder, "Low", 0, f);
    const Id vc = edit::addTrack (p, TrackKind::Track, "Cello", 0, inner);
    CHECK_EQ (p.depth (vc), 2);
    CHECK (p.isInside (vc, f));
    CHECK_EQ (p.outputOf (vc), inner);
    CHECK_EQ (p.outputOf (inner), f);
    CHECK_EQ (p.outputOf (f), Id (0));
    // The folder's block is contiguous.
    CHECK_EQ (p.indexOf (f) + 1, p.indexOf (v1));
    CHECK_EQ (p.indexOf (inner) + 1, p.indexOf (vc));

    // Moving the folder above A brings everything inside it.
    CHECK (edit::moveTrack (p, f, a, 0));
    CHECK_EQ (p.indexOf (f), 0);
    CHECK_EQ (p.indexOf (vc), 3);
    CHECK_EQ (p.indexOf (a), 4);

    // A folder can't go inside itself.
    CHECK (! edit::moveTrack (p, f, 0, inner));

    // Removing a folder keeping its contents moves them up a level.
    edit::removeTrack (p, inner, true);
    CHECK_EQ (p.track (vc)->parent, f);
    edit::removeTrack (p, f);
    CHECK (p.track (vc) == nullptr);
    CHECK_EQ (p.tracks.size(), size_t (1));
}

TEST ("folders: wrapping tracks in a new folder")
{
    Project p;
    const Id a = edit::addTrack (p, TrackKind::Track, "A");
    const Id b = edit::addTrack (p, TrackKind::Track, "B");
    const Id c = edit::addTrack (p, TrackKind::Track, "C");
    const Id f = edit::wrapInFolder (p, { b, c }, "Group");
    CHECK (f != 0);
    CHECK_EQ (p.indexOf (a), 0);
    CHECK_EQ (p.indexOf (f), 1);
    CHECK_EQ (p.track (b)->parent, f);
    CHECK_EQ (p.track (c)->parent, f);
    // And that folder inside another.
    const Id g = edit::wrapInFolder (p, { f }, "Outer");
    CHECK_EQ (p.track (f)->parent, g);
    CHECK_EQ (p.depth (c), 2);
}

TEST ("versions: a new version copies, each keeps its own clips until deleted")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Bass");
    const Id c1 = edit::addMidiClip (p, t, 0, PPQ * 4);
    edit::addNote (p, c1, { 0, 0, PPQ, 40, 100 });
    CHECK_EQ (edit::newVersion (p, t), 1);
    CHECK_EQ (p.track (t)->versions.size(), size_t (2));
    CHECK_EQ (p.track (t)->clips().size(), size_t (1));
    CHECK_EQ (p.track (t)->version().name, std::string ("Version 2"));
    // Edit version 2 only.
    const Id c2 = p.track (t)->clips().front().id;
    CHECK (c2 != c1);
    edit::addNote (p, c2, { 0, PPQ, PPQ, 43, 100 });
    CHECK_EQ (p.clip (c2)->midi.notes.size(), size_t (2));
    edit::selectVersion (p, t, 0);
    CHECK_EQ (p.track (t)->clips().front().midi.notes.size(), size_t (1));
    // Deleting version 2 removes its clips; version 1 is untouched.
    CHECK (edit::deleteVersion (p, t, 1));
    CHECK (p.clip (c2) == nullptr);
    CHECK (p.clip (c1) != nullptr);
    CHECK (! edit::deleteVersion (p, t, 0));   // the last stays
}

TEST ("clips: split keeps each half's own notes; moving between tracks")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Keys");
    const Id u = edit::addTrack (p, TrackKind::Track, "Pad");
    const Id c = edit::addMidiClip (p, t, PPQ * 4, PPQ * 8);
    edit::addNote (p, c, { 0, 0, PPQ, 60, 100 });
    edit::addNote (p, c, { 0, PPQ * 5, PPQ, 62, 100 });
    const Id r = edit::splitClip (p, c, PPQ * 8);
    CHECK (r != 0);
    CHECK_EQ (p.clip (c)->length, PPQ * 4);
    CHECK_EQ (p.clip (c)->midi.notes.size(), size_t (1));
    CHECK_EQ (p.clip (r)->offset, PPQ * 4);
    CHECK_EQ (p.clip (r)->midi.notes.size(), size_t (1));
    CHECK (edit::moveClip (p, r, PPQ * 2, u));
    const Track* owner = nullptr;
    CHECK (p.clip (r, &owner) != nullptr);
    CHECK_EQ (owner->id, u);
}

TEST ("clips: consolidating unrolls loops onto one clip")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Drums");
    const Id a = edit::addMidiClip (p, t, 0, PPQ * 4);
    edit::addNote (p, a, { 0, 0, PPQ / 2, 36, 100 });
    p.clip (a)->loopLength = PPQ;
    const Id b = edit::addMidiClip (p, t, PPQ * 8, PPQ * 2);
    edit::addNote (p, b, { 0, 0, PPQ, 38, 100 });
    const Id c = edit::consolidateClips (p, { a, b });
    CHECK (c != 0);
    CHECK_EQ (p.track (t)->clips().size(), size_t (1));
    CHECK_EQ (p.clip (c)->length, PPQ * 10);
    CHECK_EQ (p.clip (c)->midi.notes.size(), size_t (5));
}

TEST ("notes: quantise, legato, join, split, velocities")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Lead");
    const Id c = edit::addMidiClip (p, t, 0, PPQ * 4);
    const Id n1 = edit::addNote (p, c, { 0, 10, 100, 60, 100 });
    const Id n2 = edit::addNote (p, c, { 0, PPQ + 50, 100, 62, 100 });
    edit::quantise (p, c, {}, PPQ / 4);
    CHECK_EQ (p.clip (c)->midi.notes[0].start, Tick (0));
    CHECK_EQ (p.clip (c)->midi.notes[1].start, PPQ);
    edit::legato (p, c, {});
    CHECK_EQ (p.clip (c)->midi.notes[0].length, PPQ);
    const auto right = edit::splitNotes (p, c, { n1 }, PPQ / 2);
    CHECK_EQ (right.size(), size_t (1));
    CHECK_EQ (p.clip (c)->midi.notes.size(), size_t (3));
    std::set<Id> both { n1, *right.begin() };
    edit::joinNotes (p, c, both);
    CHECK_EQ (p.clip (c)->midi.notes.size(), size_t (2));
    edit::setVelocity (p, c, { n2 }, 200);
    for (auto& n : p.clip (c)->midi.notes) if (n.id == n2) CHECK_EQ (n.velocity, 127);
}

TEST ("notes: a moved group stops at the edges")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Lead");
    const Id c = edit::addMidiClip (p, t, 0, PPQ * 4);
    const Id a = edit::addNote (p, c, { 0, PPQ, PPQ, 120, 100 });
    const Id b = edit::addNote (p, c, { 0, PPQ * 2, PPQ, 100, 100 });
    edit::moveNotes (p, c, { a, b }, -PPQ * 5, 20);
    const auto& notes = p.clip (c)->midi.notes;
    CHECK_EQ (notes[0].start, Tick (0));
    CHECK_EQ (notes[0].pitch, 127);
    CHECK_EQ (notes[1].pitch, 107);
}

TEST ("devices: MIDI effects before instruments before audio effects")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Synth");
    PluginRef q4 { "VST3", "/Q4.vst3", "Pro-Q 4", "FabFilter", 1 };
    PluginRef syn { "AudioUnit", "aumu,abcd", "Synth", "Maker", 2, true };
    PluginRef arp { "CLAP", "/Arp.clap", "Arp", "Maker", 3 };
    edit::addDevice (p, t, q4, DeviceRole::AudioFx);
    edit::addDevice (p, t, syn, DeviceRole::Instrument);
    const Id a = edit::addDevice (p, t, arp, DeviceRole::MidiFx);
    const auto& d = p.track (t)->devices;
    CHECK (d[0].role == DeviceRole::MidiFx);
    CHECK (d[1].role == DeviceRole::Instrument);
    CHECK (d[2].role == DeviceRole::AudioFx);
    // Moving the arpeggiator to the end puts it back at the front.
    edit::moveDevice (p, a, 3);
    CHECK_EQ (p.track (t)->devices.front().id, a);
    // Deactivating keeps the state.
    edit::setDeviceActive (p, a, false, "c3RhdGU=");
    CHECK (! p.device (a)->active);
    CHECK_EQ (p.device (a)->state, std::string ("c3RhdGU="));
}

TEST ("routing: sends can't loop back")
{
    Project p;
    const Id a = edit::addTrack (p, TrackKind::Track, "A");
    const Id r1 = edit::addTrack (p, TrackKind::Return, "Reverb");
    const Id r2 = edit::addTrack (p, TrackKind::Return, "Delay");
    CHECK (edit::addSend (p, a, r1));
    CHECK (edit::addSend (p, r1, r2));
    CHECK (! edit::addSend (p, r2, r1));
    CHECK (! edit::addSend (p, a, a));
    const Id f = edit::addTrack (p, TrackKind::Folder, "F");
    const Id inside = edit::addTrack (p, TrackKind::Track, "In", 0, f);
    CHECK (! edit::canRoute (p, f, inside));
}

TEST ("automation: values between points, steps and curves")
{
    AutomationLane lane;
    edit::addPoint (lane, { 0, 0.0 });
    edit::addPoint (lane, { PPQ * 4, 1.0 });
    CHECK (std::abs (edit::valueAt (lane, PPQ * 2, 0) - 0.5) < 1e-9);
    lane.points[0].step = true;
    CHECK_EQ (edit::valueAt (lane, PPQ * 2, 0), 0.0);
    lane.points[0].step = false;
    lane.points[0].curve = 1.0;
    CHECK (edit::valueAt (lane, PPQ * 2, 0) < 0.5);
    edit::writeValue (lane, PPQ, PPQ * 3, 0.25);
    CHECK_EQ (lane.points.size(), size_t (3));
    CHECK (std::abs (edit::valueAt (lane, PPQ * 3, 0) - 0.25) < 1e-9);
}

TEST ("undo: one step per push, redo after undo")
{
    Project p;
    edit::History h;
    h.push (p, "Add Track");
    edit::addTrack (p, TrackKind::Track, "A");
    h.push (p, "Add Track");
    edit::addTrack (p, TrackKind::Track, "B");
    CHECK (h.undo (p));
    CHECK_EQ (p.tracks.size(), size_t (1));
    CHECK (h.redo (p));
    CHECK_EQ (p.tracks.size(), size_t (2));
    CHECK (h.undo (p));
    CHECK (h.undo (p));
    CHECK (p.tracks.empty());
    CHECK (! h.canUndo());
}

TEST ("automation: trim moves what was written by the hand's change, keeping its shape")
{
    AutomationLane lane;
    edit::addPoint (lane, { 0, 0.2 });
    edit::addPoint (lane, { PPQ * 2, 0.6 });
    edit::addPoint (lane, { PPQ * 4, 0.2 });
    const auto original = lane;
    edit::writeTrim (lane, original, PPQ, PPQ * 3, 0.1);
    CHECK (std::abs (edit::valueAt (lane, PPQ * 2, 0) - 0.7) < 1e-9);
    CHECK (std::abs (edit::valueAt (lane, PPQ, 0) - 0.5) < 1e-9);       // 0.4 on the slope, plus 0.1
    CHECK (std::abs (edit::valueAt (lane, PPQ * 4, 0) - 0.2) < 1e-9);   // outside: untouched
}

TEST ("automation: copied points paste at another place, the rest carrying on")
{
    AutomationLane lane;
    edit::addPoint (lane, { 0, 0.0 });
    edit::addPoint (lane, { PPQ, 1.0 });
    edit::addPoint (lane, { PPQ * 8, 0.5 });
    const auto pts = edit::copyPoints (lane, 0, PPQ * 2);
    CHECK_EQ (pts.front().tick, (Tick) 0);
    CHECK_EQ (pts.front().value, 0.0);
    edit::pastePoints (lane, pts, PPQ * 4, PPQ * 2);
    CHECK (std::abs (edit::valueAt (lane, PPQ * 5, 0) - 1.0) < 1e-9);
    CHECK (std::abs (edit::valueAt (lane, PPQ * 4, 0) - 0.0) < 1e-9);
    CHECK (std::abs (edit::valueAt (lane, PPQ * 8, 0) - 0.5) < 1e-9);
}

TEST ("versions: four at most; each keeps its own devices, under the same ids, and the lanes follow")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Keys");
    PluginRef piano;  piano.format = "VST3"; piano.identifier = "piano"; piano.name = "Piano";
    PluginRef organ;  organ.format = "VST3"; organ.identifier = "organ"; organ.name = "Organ";
    PluginRef verb;   verb.format = "VST3"; verb.identifier = "verb"; verb.name = "Verb";
    const Id inst = edit::addDevice (p, t, piano, DeviceRole::Instrument);
    AutomationLane lane;
    lane.target.kind = AutoTarget::Kind::DeviceParam;
    lane.target.device = inst;
    lane.target.param = 0;
    p.track (t)->automation.push_back (lane);

    // Version 2 starts with a copy of version 1's devices, under the same ids.
    CHECK_EQ (edit::newVersion (p, t), 1);
    CHECK_EQ (p.track (t)->devices.size(), size_t (1));
    CHECK_EQ (p.track (t)->devices.front().id, inst);
    // Changed in version 2 only: a different instrument and an effect.
    p.track (t)->devices.front().plugin = organ;
    const Id fx = edit::addDevice (p, t, verb, DeviceRole::AudioFx);
    CHECK_EQ (p.track (t)->devices.size(), size_t (2));

    edit::selectVersion (p, t, 0);
    CHECK_EQ (p.track (t)->devices.size(), size_t (1));
    CHECK_EQ (p.track (t)->devices.front().plugin.name, std::string ("Piano"));
    CHECK (p.device (fx) == nullptr);            // version 2's, waiting
    CHECK (p.track (t)->version().devices.empty());
    edit::selectVersion (p, t, 1);
    CHECK_EQ (p.track (t)->devices.front().plugin.name, std::string ("Organ"));
    CHECK (p.device (fx) != nullptr);

    // Removing the instrument in version 2 keeps the lane: version 1 still has it.
    edit::removeDevice (p, inst);
    CHECK_EQ (p.track (t)->automation.size(), size_t (1));

    // Four at most.
    CHECK_EQ (edit::newVersion (p, t), 2);
    CHECK_EQ (edit::newEmptyVersion (p, t), 3);
    CHECK_EQ (edit::newVersion (p, t), -1);
    CHECK_EQ (edit::newEmptyVersion (p, t), -1);
    CHECK_EQ (p.track (t)->versions.size(), size_t (4));

    // Deleting the active version leaves the one before it active, with its own devices.
    edit::selectVersion (p, t, 1);
    CHECK (edit::deleteVersion (p, t, 1));
    CHECK_EQ (p.track (t)->activeVersion, 0);
    CHECK_EQ (p.track (t)->devices.front().plugin.name, std::string ("Piano"));
    // Deleting another keeps the active one's devices where they are.
    CHECK (edit::deleteVersion (p, t, 2));
    CHECK_EQ (p.track (t)->devices.front().plugin.name, std::string ("Piano"));
}

TEST ("versions: a version made before they had their own devices takes the track's when chosen")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Old");
    PluginRef piano;  piano.format = "VST3"; piano.identifier = "piano"; piano.name = "Piano";
    edit::addDevice (p, t, piano, DeviceRole::Instrument);
    TrackVersion old;
    old.id = p.newId();
    old.name = "Version 2";
    p.track (t)->versions.push_back (old);   // as an older file has it: no devices of its own
    edit::selectVersion (p, t, 1);
    CHECK_EQ (p.track (t)->devices.size(), size_t (1));
    CHECK_EQ (p.track (t)->devices.front().plugin.name, std::string ("Piano"));
}

TEST ("tracks: a Summing track takes no input; there's one Cue track, after the master, holding no clips and only audio effects")
{
    Project p;
    const Id sum = edit::addTrack (p, TrackKind::Summing, "Summing");
    CHECK (p.track (sum)->holdsClips());
    CHECK_EQ (p.track (sum)->input.audioChannel, -1);
    CHECK (edit::addMidiClip (p, sum, 0, PPQ) != 0);   // it holds clips like any track

    const Id cue = edit::addCue (p);
    CHECK_EQ (edit::addTrack (p, TrackKind::Cue, "Another"), cue);   // still one
    CHECK_EQ (p.cue.size(), size_t (1));
    CHECK (p.track (cue) != nullptr);
    CHECK (p.indexOf (cue) < 0);                         // not among the tracks
    CHECK_EQ (p.outputOf (cue), Id (0));
    CHECK_EQ (edit::addMidiClip (p, cue, 0, PPQ), Id (0));
    CHECK_EQ (edit::newVersion (p, cue), -1);
    PluginRef synth;  synth.format = "VST3"; synth.identifier = "s"; synth.name = "Synth";
    PluginRef eq;     eq.format = "VST3"; eq.identifier = "e"; eq.name = "EQ";
    CHECK_EQ (edit::addDevice (p, cue, synth, DeviceRole::Instrument), Id (0));
    const Id d = edit::addDevice (p, cue, eq, DeviceRole::AudioFx);
    CHECK (d != 0);
    CHECK (p.device (d) != nullptr);
    edit::removeTrack (p, cue);
    CHECK (p.cue.empty());
}

#include "ProjectFile.h"

TEST ("song versions: 2 starts as a copy of 1; each has its own tracks, tempo, markers and key; the name, details and timer are the song's (0103)")
{
    Project p;
    p.name = "Song";
    p.metadata.artist = "Me";
    p.workSeconds = 60;
    const Id t = edit::addTrack (p, TrackKind::Track, "Bass");
    p.tempo.setTempos ({ { 0, 100.0, false } });
    CHECK_EQ (p.songVersions(), 1);
    CHECK (! edit::selectSongVersion (p, 1));   // there's no version 2 yet

    CHECK (edit::newSongVersion (p));
    CHECK_EQ (p.songVersions(), 2);
    CHECK_EQ (p.songVersion, 1);                // the copy is chosen
    CHECK (p.track (t) != nullptr);
    CHECK (! edit::newSongVersion (p));         // two at most
    // Version 2 changes on its own.
    edit::addTrack (p, TrackKind::Track, "Drums");
    p.tempo.setTempos ({ { 0, 140.0, false } });
    p.markers.push_back ({ p.newId(), PPQ * 4, "Drop" });
    p.scale.root = 5;
    p.name = "Song Renamed";
    p.workSeconds = 90;

    CHECK (edit::selectSongVersion (p, 0));
    CHECK_EQ (p.songVersion, 0);
    CHECK_EQ (p.tracks.size(), (size_t) 1);
    CHECK (std::abs (p.tempo.bpmAt (0) - 100.0) < 1e-9);
    CHECK (p.markers.empty());
    CHECK_EQ (p.scale.root, 0);
    CHECK_EQ (p.name, std::string ("Song Renamed"));   // the song's, not a version's
    CHECK_EQ (p.metadata.artist, std::string ("Me"));
    CHECK (std::abs (p.workSeconds - 90) < 1e-9);
    const Id fresh = p.newId();

    CHECK (edit::selectSongVersion (p, 1));
    CHECK_EQ (p.tracks.size(), (size_t) 2);
    CHECK (std::abs (p.tempo.bpmAt (0) - 140.0) < 1e-9);
    CHECK_EQ (p.markers.size(), (size_t) 1);
    CHECK_EQ (p.scale.root, 5);
    CHECK (p.newId() > fresh);                  // ids are never handed out twice across versions

    // Saved and opened: both versions, the chosen one playing.
    auto back = file::load (file::save (p));
    CHECK (back.has_value());
    if (! back) return;
    CHECK_EQ (back->songVersions(), 2);
    CHECK_EQ (back->songVersion, 1);
    CHECK_EQ (back->tracks.size(), (size_t) 2);
    CHECK (edit::selectSongVersion (*back, 0));
    CHECK_EQ (back->tracks.size(), (size_t) 1);

    // Deleting the playing version leaves the other, as version 1.
    CHECK (edit::deleteSongVersion (p, 1));
    CHECK_EQ (p.songVersions(), 1);
    CHECK_EQ (p.songVersion, 0);
    CHECK_EQ (p.tracks.size(), (size_t) 1);
}

TEST ("song versions: the one playing is known by its id, which changes only when the other is chosen; a frozen render the other uses is found (0103)")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Keys");
    p.track (t)->frozen = true;
    p.track (t)->frozenFile = "/songs/Keys frozen.wav";
    const Id first = p.songId;
    CHECK (! edit::otherVersionUses (p, "/songs/Keys frozen.wav"));
    edit::newSongVersion (p);
    CHECK_EQ (p.songId, first);                 // the copy plays on: nothing needs loading again
    CHECK (edit::otherVersionUses (p, "/songs/Keys frozen.wav"));
    CHECK (! edit::otherVersionUses (p, "/songs/other.wav"));
    edit::selectSongVersion (p, 0);
    CHECK (p.songId != first);
    edit::selectSongVersion (p, 1);
    CHECK_EQ (p.songId, first);
    // Deleting the one not playing changes nothing that plays.
    edit::deleteSongVersion (p, 0);
    CHECK_EQ (p.songId, first);
}

TEST ("transport: the end of the song is where its last clip ends, on any track; 0 with none (0107)")
{
    Project p;
    CHECK_EQ (p.lastClipEnd(), (Tick) 0);
    const Id a = edit::addTrack (p, TrackKind::Track, "A");
    const Id b = edit::addTrack (p, TrackKind::Track, "B");
    edit::addMidiClip (p, a, 0, PPQ * 4);
    edit::addMidiClip (p, b, PPQ * 6, PPQ * 3);
    edit::addMidiClip (p, a, PPQ * 2, PPQ * 2);
    CHECK_EQ (p.lastClipEnd(), PPQ * 9);
    // A version not playing doesn't count; muted clips do (they're on the timeline).
    edit::newVersion (p, b);
    p.track (b)->clips().clear();
    CHECK_EQ (p.lastClipEnd(), PPQ * 4);
}

TEST ("export: the whole song ends at the bar its last clip ends in, not the view's 16 bars (the UAT's 4 bars exported as 16)")
{
    Project p;
    p.tempo.setTempoAt (0, 100.0);
    CHECK_EQ (p.songEnd(), p.tempo.barStart (1));                 // nothing: one bar
    const Id a = edit::addTrack (p, TrackKind::Track, "A");
    edit::addMidiClip (p, a, 0, PPQ * 16);                        // 4 bars of 4/4
    CHECK_EQ (p.songEnd(), p.tempo.barStart (4));                 // to the end of bar 4
    CHECK (p.endTick() > p.songEnd());                            // the view keeps its 16
    edit::addMidiClip (p, a, PPQ * 16, PPQ * 3);                  // into bar 5
    CHECK_EQ (p.songEnd(), p.tempo.barStart (5));                 // to its bar line
}
