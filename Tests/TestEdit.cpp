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
