// DAWproject in and out (decision 0077).
#include "Check.h"
#include "DawProject.h"
#include "Edit.h"

#include <cmath>

using namespace kal;

static const Track* named (const Project& p, const std::string& n)
{
    for (auto& t : p.tracks) if (t.name == n) return &t;
    return nullptr;
}

TEST ("dawproject: a song written and read back keeps its tracks, mix, tempo, time signatures, markers, clips and devices")
{
    Project p;
    p.tempo.setTempoAt (0, 96);
    p.tempo.setMeterAt (4, 3, 4);
    p.tempo.setTempoAt (p.tempo.barStart (8), 140, true);
    p.markers.push_back ({ p.newId(), PPQ * 8, "Chorus" });
    const Id band = edit::addTrack (p, TrackKind::Folder, "Band");
    const Id keys = edit::addTrack (p, TrackKind::Track, "Keys", 0, band);
    const Id verb = edit::addTrack (p, TrackKind::Return, "Reverb");
    auto* k = p.track (keys);
    k->volumeDb = -6.0;
    k->pan = -0.5;
    k->mute = true;
    k->colour = 0xff336699;
    edit::addSend (p, keys, verb, -12.0);
    const Id synth = edit::addDevice (p, keys, PluginRef { "Kalibre", "synth", "Kalibre Synth" }, DeviceRole::Instrument);
    const Id eq = edit::addDevice (p, keys, PluginRef { "VST3", "/Library/Audio/Plug-Ins/VST3/Pro-Q 3.vst3", "Pro-Q 3", "FabFilter" }, DeviceRole::AudioFx);
    for (auto& d : k->devices) d.state = d.id == synth ? "abc123" : "zzz999";
    const Id clip = edit::addMidiClip (p, keys, PPQ * 4, PPQ * 8);
    auto* c = p.clip (clip);
    c->offset = PPQ;
    c->loopLength = PPQ * 4;
    for (int i = 0; i < 3; ++i) { Note n; n.start = PPQ * i; n.length = PPQ / 2; n.pitch = 60 + i; n.velocity = 100; edit::addNote (p, clip, n); }
    c = p.clip (clip);
    c->midi.events.push_back ({ PPQ, EventType::Controller, 0, 1, 64 });
    const Id vox = edit::addTrack (p, TrackKind::Track, "Vocal");
    Clip a;
    a.kind = Clip::Kind::Audio;
    a.start = PPQ * 16;
    a.length = PPQ * 4;
    a.audio.file = "Audio/take 1.wav";
    a.audio.sourceStart = 1.5;
    a.audio.sourceLength = 2.0;
    a.audio.fadeIn = 0.25;
    edit::addClip (p, vox, a);

    const auto w = dawproject::write (p, "1.0");
    CHECK (w.projectXml.find ("<Project version=\"1.0\">") != std::string::npos);
    CHECK_EQ (w.audio.size(), size_t (1));
    if (! w.audio.empty()) { CHECK_EQ (w.audio[0].first, std::string ("audio/take 1.wav")); CHECK_EQ (w.audio[0].second, std::string ("Audio/take 1.wav")); }
    CHECK_EQ (w.states.size(), size_t (2));
    CHECK (w.metadataXml.find ("<MetaData>") != std::string::npos);

    const auto r = dawproject::read (w.projectXml, w.metadataXml);
    CHECK (r.ok);
    if (! r.ok) return;
    const auto& q = r.project;
    CHECK_EQ (q.tempo.bpmAt (0), 96.0);
    CHECK_EQ (q.tempo.meterAtBar (5).numerator, 3);
    CHECK (std::abs (q.tempo.bpmAt (q.tempo.barStart (8)) - 140.0) < 0.01);
    CHECK_EQ (q.markers.size(), size_t (1));
    const auto* qb = named (q, "Band");
    const auto* qk = named (q, "Keys");
    const auto* qv = named (q, "Reverb");
    CHECK (qb && qk && qv);
    if (! (qb && qk && qv)) return;
    CHECK (qb->isFolder());
    CHECK_EQ (qk->parent, qb->id);
    CHECK (qv->kind == TrackKind::Return);
    CHECK (std::abs (qk->volumeDb + 6.0) < 0.01);
    CHECK (std::abs (qk->pan + 0.5) < 0.001);
    CHECK (qk->mute);
    CHECK_EQ (qk->colour, std::uint32_t (0xff336699));
    CHECK_EQ (qk->sends.size(), size_t (1));
    if (! qk->sends.empty()) { CHECK_EQ (qk->sends[0].target, qv->id); CHECK (std::abs (qk->sends[0].gainDb + 12.0) < 0.01); }
    CHECK_EQ (qk->devices.size(), size_t (2));
    if (qk->devices.size() == 2)
    {
        CHECK_EQ (qk->devices[0].plugin.format, std::string ("Kalibre"));
        CHECK_EQ (qk->devices[0].plugin.identifier, std::string ("synth"));
        CHECK (r.states.count (qk->devices[0].id) == 1);
        CHECK_EQ (qk->devices[1].plugin.name, std::string ("Pro-Q 3"));
        CHECK (qk->devices[1].role == DeviceRole::AudioFx);
        CHECK (r.kalibreStates.count (qk->devices[1].id) == 1);
    }
    CHECK_EQ (qk->clips().size(), size_t (1));
    if (! qk->clips().empty())
    {
        const auto& qc = qk->clips().front();
        CHECK_EQ (qc.start, Tick (PPQ * 4));
        CHECK_EQ (qc.length, Tick (PPQ * 8));
        CHECK_EQ (qc.offset, Tick (PPQ));
        CHECK_EQ (qc.loopLength, Tick (PPQ * 4));
        CHECK_EQ (qc.midi.notes.size(), size_t (3));
        CHECK_EQ (qc.midi.notes[2].pitch, 62);
        CHECK_EQ (qc.midi.notes[2].start, Tick (PPQ * 2));
        CHECK_EQ (qc.midi.notes[2].velocity, 100);
        CHECK_EQ (qc.midi.events.size(), size_t (1));
    }
    const auto* qvox = named (q, "Vocal");
    CHECK (qvox != nullptr);
    if (qvox && ! qvox->clips().empty())
    {
        const auto& ac = qvox->clips().front();
        CHECK (! ac.isMidi());
        CHECK_EQ (ac.audio.file, std::string ("audio/take 1.wav"));
        CHECK (std::abs (ac.audio.sourceStart - 1.5) < 0.0001);
        CHECK (std::abs (ac.audio.fadeIn - 0.25) < 0.0001);
        CHECK_EQ (ac.start, Tick (PPQ * 16));
    }
}

TEST ("dawproject: Bitwig's way of writing it: nested lanes, notes in a clip's lanes, audio in warps, a clip of clips")
{
    const std::string xml = R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Project version="1.0">
  <Application name="Bitwig Studio" version="5.0"/>
  <Transport><Tempo unit="bpm" value="128" id="id0" name="Tempo"/><TimeSignature numerator="4" denominator="4" id="id1"/></Transport>
  <Structure>
    <Track contentType="notes" loaded="true" id="id2" name="Lead" color="#d92e24">
      <Channel audioChannels="2" destination="id9" role="regular" solo="false" id="id3">
        <Devices>
          <ClapPlugin deviceID="com.u-he.Diva" deviceName="Diva" deviceRole="instrument" deviceVendor="u-he" loaded="true" id="id4" name="Diva">
            <Enabled value="true" id="id5" name="On/Off"/>
            <State path="plugins/x.clap-preset"/>
          </ClapPlugin>
          <BuiltinDevice deviceName="EQ+" deviceRole="audioFX" deviceVendor="Bitwig" id="id6" name="EQ+"/>
        </Devices>
        <Mute value="false" id="id7" name="Mute"/>
        <Pan max="1.0" min="0.0" unit="normalized" value="0.5" id="id8" name="Pan"/>
        <Volume max="2.0" min="0.0" unit="linear" value="0.5" id="id10" name="Volume"/>
      </Channel>
    </Track>
    <Track contentType="audio" loaded="true" id="id11" name="Drums">
      <Channel audioChannels="2" destination="id9" role="regular" solo="true" id="id12"/>
    </Track>
    <Track contentType="audio notes" loaded="true" id="id13" name="Master">
      <Channel audioChannels="2" role="master" solo="false" id="id9"/>
    </Track>
  </Structure>
  <Arrangement id="id14">
    <Lanes timeUnit="beats" id="id15">
      <Lanes track="id2" id="id16">
        <Clips id="id17">
          <Clip time="4.0" duration="8.0" playStart="0.0">
            <Clips id="id18">
              <Clip time="0.0" duration="4.0" playStart="0.0">
                <Lanes id="id19">
                  <Notes id="id20"><Note time="0.0" duration="1.0" channel="0" key="69" vel="0.5" rel="0.5"/></Notes>
                </Lanes>
              </Clip>
            </Clips>
          </Clip>
        </Clips>
      </Lanes>
      <Lanes track="id11" id="id21">
        <Clips id="id22">
          <Clip time="0.0" duration="4.0" playStart="0.0" contentTimeUnit="seconds" fadeTimeUnit="beats" fadeInTime="0.5" fadeOutTime="0.0">
            <Warps contentTimeUnit="seconds" timeUnit="beats" id="id23">
              <Audio channels="2" duration="1.875" sampleRate="44100" algorithm="stretch" id="id24"><File path="audio/drums.wav"/></Audio>
              <Warp time="0.0" contentTime="0.0"/>
              <Warp time="4.0" contentTime="1.875"/>
            </Warps>
          </Clip>
        </Clips>
      </Lanes>
    </Lanes>
  </Arrangement>
</Project>)";
    const auto r = dawproject::read (xml);
    CHECK (r.ok);
    const auto& q = r.project;
    CHECK_EQ (q.tempo.bpmAt (0), 128.0);
    const auto* lead = named (q, "Lead");
    const auto* drums = named (q, "Drums");
    CHECK (lead && drums);
    if (! lead || ! drums) return;
    CHECK_EQ (q.tracks.size(), size_t (2));          // the master is Kalibre's own
    CHECK (std::abs (lead->volumeDb + 6.02) < 0.05);
    CHECK_EQ (lead->devices.size(), size_t (1));      // Bitwig's EQ+ left out
    if (! lead->devices.empty()) { CHECK_EQ (lead->devices[0].plugin.format, std::string ("CLAP")); CHECK_EQ (lead->devices[0].plugin.name, std::string ("Diva")); }
    CHECK (! r.leftOut.empty());
    CHECK_EQ (lead->clips().size(), size_t (1));
    if (! lead->clips().empty())
    {
        CHECK_EQ (lead->clips()[0].start, Tick (PPQ * 4));
        CHECK_EQ (lead->clips()[0].midi.notes.size(), size_t (1));
        if (! lead->clips()[0].midi.notes.empty()) CHECK_EQ (lead->clips()[0].midi.notes[0].velocity, 64);
    }
    CHECK (drums->solo);
    CHECK_EQ (drums->clips().size(), size_t (1));
    if (! drums->clips().empty())
    {
        CHECK (! drums->clips()[0].isMidi());
        CHECK_EQ (drums->clips()[0].audio.file, std::string ("audio/drums.wav"));
        CHECK (std::abs (drums->clips()[0].audio.fadeIn - 0.5 * 60.0 / 128.0) < 0.001);
    }
    CHECK (! dawproject::read ("<NotAProject/>").ok);
}

#include "AudioTools.h"

TEST ("dawproject: automation (volume, pan, mute, sends, plug-ins' parameters, the master) and warping, written and read back (0084)")
{
    Project p;
    p.tempo.setTempoAt (0, 120);
    const Id keys = edit::addTrack (p, TrackKind::Track, "Keys");
    const Id verb = edit::addTrack (p, TrackKind::Return, "Reverb");
    edit::addSend (p, keys, verb, -12.0);
    const Id synth = edit::addDevice (p, keys, PluginRef { "Kalibre", "synth", "Kalibre Synth" }, DeviceRole::Instrument);
    auto lane = [&] (Track& t, AutoTarget target, std::vector<AutoPoint> pts)
    {
        AutomationLane l;
        l.id = p.newId();
        l.target = target;
        l.points = std::move (pts);
        t.automation.push_back (l);
    };
    AutoTarget vol; vol.kind = AutoTarget::Kind::Volume;
    AutoTarget pan; pan.kind = AutoTarget::Kind::Pan;
    AutoTarget mute; mute.kind = AutoTarget::Kind::Mute;
    AutoTarget send; send.kind = AutoTarget::Kind::SendLevel; send.send = verb;
    AutoTarget param; param.kind = AutoTarget::Kind::DeviceParam; param.device = synth; param.param = 3; param.paramName = "Cutoff";
    lane (*p.track (keys), vol, { { 0, 0.9 }, { PPQ * 4, 0.5 }, { PPQ * 8, 0.75, 0, true } });
    lane (*p.track (keys), pan, { { 0, 0.5 }, { PPQ * 2, 0.0 } });
    lane (*p.track (keys), mute, { { 0, 0.0, 0, true }, { PPQ * 6, 1.0, 0, true } });
    lane (*p.track (keys), send, { { 0, 0.3 }, { PPQ * 4, 0.6 } });
    lane (*p.track (keys), param, { { PPQ, 0.2 }, { PPQ * 3, 0.8 } });
    lane (p.master, vol, { { 0, 0.8 }, { PPQ * 16, 0.6 } });

    // Warped audio: by markers, and by the file's own tempo alone.
    const Id vox = edit::addTrack (p, TrackKind::Track, "Vocal");
    Clip a;
    a.kind = Clip::Kind::Audio;
    a.start = PPQ * 4;
    a.length = PPQ * 4;
    a.offset = PPQ;
    a.audio.file = "Audio/vox.wav";
    a.audio.sourceLength = 3.0;
    a.audio.warp = WarpMode::Spectral;
    a.audio.markers = { { 0.0, 0 }, { 1.0, PPQ * 2 }, { 2.5, PPQ * 5 } };
    edit::addClip (p, vox, a);
    Clip b = a;
    b.start = PPQ * 12;
    b.offset = 0;
    b.audio.markers.clear();
    b.audio.warp = WarpMode::Repitch;
    b.audio.sourceBpm = 100;
    b.audio.sourceStart = 0.5;
    b.loopLength = PPQ * 2;
    b.length = PPQ * 4;
    edit::addClip (p, vox, b);

    const auto w = dawproject::write (p, "1.0");
    CHECK (w.projectXml.find ("<Points") != std::string::npos);
    CHECK (w.projectXml.find ("<Warps") != std::string::npos);
    const auto r = dawproject::read (w.projectXml, w.metadataXml);
    CHECK (r.ok);
    for (auto& s : r.leftOut) CHECK (s.find ("automation") == std::string::npos && s.find ("warping") == std::string::npos);
    const auto& q = r.project;
    const auto* k = named (q, "Keys");
    const auto* rv = named (q, "Reverb");
    CHECK (k && rv);
    if (! k || ! rv) return;
    auto find = [] (const Track& t, AutoTarget::Kind kind) -> const AutomationLane*
    {
        for (auto& l : t.automation) if (l.target.kind == kind) return &l;
        return nullptr;
    };
    auto same = [] (const AutomationLane* l, std::vector<AutoPoint> want)
    {
        if (! l || l->points.size() != want.size()) return false;
        for (size_t i = 0; i < want.size(); ++i)
            if (l->points[i].tick != want[i].tick || std::abs (l->points[i].value - want[i].value) > 1.0e-4 || l->points[i].step != want[i].step) return false;
        return true;
    };
    CHECK (same (find (*k, AutoTarget::Kind::Volume), { { 0, 0.9 }, { PPQ * 4, 0.5 }, { PPQ * 8, 0.75, 0, true } }));
    CHECK (same (find (*k, AutoTarget::Kind::Pan), { { 0, 0.5 }, { PPQ * 2, 0.0 } }));
    CHECK (same (find (*k, AutoTarget::Kind::Mute), { { 0, 0.0, 0, true }, { PPQ * 6, 1.0, 0, true } }));
    const auto* s = find (*k, AutoTarget::Kind::SendLevel);
    CHECK (same (s, { { 0, 0.3 }, { PPQ * 4, 0.6 } }));
    if (s) CHECK_EQ (s->target.send, rv->id);
    const auto* pl = find (*k, AutoTarget::Kind::DeviceParam);
    CHECK (same (pl, { { PPQ, 0.2 }, { PPQ * 3, 0.8 } }));
    if (pl && ! k->devices.empty())
    {
        CHECK_EQ (pl->target.device, k->devices[0].id);
        CHECK_EQ (pl->target.param, 3);
        CHECK_EQ (pl->target.paramName, std::string ("Cutoff"));
    }
    CHECK (same (find (q.master, AutoTarget::Kind::Volume), { { 0, 0.8 }, { PPQ * 16, 0.6 } }));

    const auto* v = named (q, "Vocal");
    CHECK (v != nullptr);
    if (! v || v->clips().size() != 2) { CHECK (false); return; }
    const auto& ca = v->clips()[0];
    CHECK_EQ (ca.audio.warp, WarpMode::Spectral);
    CHECK_EQ (ca.offset, Tick (PPQ));
    CHECK (ca.audio.markers == a.audio.markers);
    const auto& cb = v->clips()[1];
    CHECK_EQ (cb.audio.warp, WarpMode::Repitch);
    CHECK_EQ (cb.loopLength, Tick (PPQ * 2));
    // The same moment of the file at each end of the content.
    for (Tick t : { Tick (0), Tick (PPQ), Tick (PPQ * 2) })
        CHECK (std::abs (audiotools::sourceSecondsAt (cb.audio, t) - audiotools::sourceSecondsAt (b.audio, t)) < 1.0e-6);
}

TEST ("dawproject: automation curves go out as points other programs follow, and come back to Kalibre as the curves they were (0087)")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Pad");
    AutomationLane l;
    l.id = p.newId();
    l.target.kind = AutoTarget::Kind::Pan;
    l.points = { { 0, 0.0, 0.6 }, { PPQ * 4, 1.0, -0.4 }, { PPQ * 8, 0.2 } };
    p.track (t)->automation.push_back (l);
    const auto w = dawproject::write (p, "1.0");
    CHECK (! w.kalibreAutomation.empty());
    // Another program: the bend drawn as straight pieces close to it.
    auto r = dawproject::read (w.projectXml, w.metadataXml);
    const auto* pad = named (r.project, "Pad");
    CHECK (pad && ! pad->automation.empty());
    if (! pad || pad->automation.empty()) return;
    const auto& got = pad->automation.front();
    CHECK (got.points.size() > 6);
    for (Tick at : { PPQ, PPQ * 2, PPQ * 3, PPQ * 5, PPQ * 7 })
        CHECK (std::abs (edit::valueAt (got, at, 0) - edit::valueAt (l, at, 0)) < 0.02);
    // Kalibre, with its own note of them: exactly as they were.
    r = dawproject::read (w.projectXml, w.metadataXml, w.kalibreAutomation);
    pad = named (r.project, "Pad");
    CHECK (pad && ! pad->automation.empty());
    if (pad && ! pad->automation.empty()) CHECK (pad->automation.front().points == l.points);
}
