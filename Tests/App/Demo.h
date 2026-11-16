// A small song to look at and test against: folders inside folders, MIDI on
// several tracks, a drum pattern, versions, an articulation map, a return, the
// X/Y morph, automation and a modulator.
#pragma once

#include "Edit.h"
#include "MorphProcessor.h"
#include "Patterns.h"

namespace kal::demo
{
inline Project song()
{
    Project p;
    p.name = "Demo";
    p.tempo.setTempoAt (0, 96);
    p.scale.root = 13;   // A
    p.scale.scale = 1;   // Minor (Natural)
    p.loop = { 0, PPQ * 16, false };

    const Id band = edit::addTrack (p, TrackKind::Folder, "Band");
    const Id keys = edit::addTrack (p, TrackKind::Track, "Keys", 0, band);
    const Id bass = edit::addTrack (p, TrackKind::Track, "Bass", 0, band);
    const Id rhythm = edit::addTrack (p, TrackKind::Folder, "Rhythm", 0, band);
    const Id drums = edit::addTrack (p, TrackKind::Track, "Drums", 0, rhythm);
    const Id strings = edit::addTrack (p, TrackKind::Track, "Strings");
    const Id verb = edit::addTrack (p, TrackKind::Return, "Return A");
    p.track (keys)->colour = 0xff6FA8DC;
    p.track (bass)->colour = 0xff9DCB7A;
    p.track (drums)->colour = 0xffE3C26F;
    p.track (strings)->colour = 0xffB892D9;
    p.track (strings)->delayMs = -125;

    // Am  F  C  G, a bar each, twice.
    const int chords[4][3] { { 57, 60, 64 }, { 53, 57, 60 }, { 55, 60, 64 }, { 55, 59, 62 } };
    const int roots[4] { 45, 41, 48, 43 };
    const Id kc = edit::addMidiClip (p, keys, 0, PPQ * 16);
    const Id bc = edit::addMidiClip (p, bass, 0, PPQ * 16);
    for (int bar = 0; bar < 4; ++bar)
    {
        for (int n : chords[bar]) edit::addNote (p, kc, { 0, bar * PPQ * 4, PPQ * 4 - PPQ / 8, n, 84 });
        for (int beat = 0; beat < 4; ++beat)
            edit::addNote (p, bc, { 0, bar * PPQ * 4 + beat * PPQ, PPQ / 2, roots[bar] - 12 + (beat == 3 ? 7 : 0), 100 });
    }
    p.clip (kc)->name = "Chords";
    p.clip (bc)->name = "Bass line";

    const Id dc = edit::addMidiClip (p, drums, 0, PPQ * 16);
    p.clip (dc)->name = "Beat";
    patterns::Grid g;
    g.lanes = patterns::standardKit (16);
    for (auto& lane : g.lanes)
    {
        if (lane.pitch == 36) patterns::euclidLane (lane, 16, 4, 0, 110);
        if (lane.pitch == 38) for (int s : { 4, 12 }) lane.velocity[(size_t) s] = 100;
        if (lane.pitch == 42) for (int s = 0; s < 16; s += 2) lane.velocity[(size_t) s] = 70 + (s % 4 == 0 ? 20 : 0);
    }
    unsigned next = p.nextId;
    patterns::writeGrid (p.clip (dc)->midi, g, 0, PPQ * 16, next);
    p.nextId = next;

    ArticulationMap m;
    m.id = p.newId();
    m.name = "Strings";
    m.articulations = { { "Legato", 24, 100, -1, 0, -1, -1, -80, 0xff6FA8DC }, { "Staccato", 26, 100, -1, 0, -1, -1, -20, 0xffE3C26F } };
    p.articulationMaps.push_back (m);
    p.track (strings)->articulationMap = m.id;
    const Id sc = edit::addMidiClip (p, strings, PPQ * 8, PPQ * 8);
    p.clip (sc)->name = "Line";
    int pitch[] { 76, 74, 72, 71, 72, 74, 76, 79 };
    for (int i = 0; i < 8; ++i)
    {
        Note n { 0, i * PPQ, PPQ - 40, pitch[i], 90 };
        n.articulation = i < 4 ? 0 : 1;
        edit::addNote (p, sc, n);
    }
    edit::newVersion (p, strings);
    edit::selectVersion (p, strings, 0);

    edit::addDevice (p, keys, morphRef(), DeviceRole::Instrument);
    edit::addSend (p, keys, verb, -12);
    edit::addSend (p, strings, verb, -6);
    AutoTarget vol;
    vol.kind = AutoTarget::Kind::Volume;
    auto& lane = edit::laneFor (*p.track (keys), vol, 0.9);
    lane.id = p.newId();
    edit::addPoint (lane, { PPQ * 8, 0.9 });
    edit::addPoint (lane, { PPQ * 16, 0.6 });
    p.markers.push_back ({ p.newId(), PPQ * 8, "B section" });
    return p;
}
} // namespace kal::demo
