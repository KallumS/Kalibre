#include "Surfaces.h"

#include "Mackie.h"

namespace kal
{
using namespace juce;
namespace note = mackie::note;

Surfaces::Surfaces (Controller& ctrl) : c (ctrl)
{
    c.engine.surfaceInput = [this] (const String& device, const MidiMessage& m)
    {
        {
            const SpinLock::ScopedLockType sl (inputLock);
            if (mackieIn.isEmpty() || device != mackieIn) return false;
        }
        MessageManager::callAsync ([this, m] { mackie (m); });
        return true;
    };
    c.onStudioChanged = [this] { configure(); };
    configure();
    startTimerHz (20);
}

Surfaces::~Surfaces()
{
    stopTimer();
    c.engine.surfaceInput = nullptr;
    c.onStudioChanged = nullptr;
    if (oscIn) oscIn->disconnect();
}

void Surfaces::configure()
{
    const auto& s = c.studio;
    {
        const SpinLock::ScopedLockType sl (inputLock);
        mackieIn = String (s.mackieIn);
    }
    mackieOut.reset();
    if (! s.mackieOut.empty()) mackieOut = MidiOutput::openDevice (String (s.mackieOut));
    shown = {};
    playShown = recordShown = cycleShown = -1;
    timeShown.clear();
    if (mackieOut) feedback (true);

    if (oscIn) { oscIn->disconnect(); oscIn.reset(); }
    oscOut.disconnect();
    oscConnected = false;
    oscShown.clear();
    if (s.oscOn)
    {
        oscIn = std::make_unique<OSCReceiver>();
        if (oscIn->connect (s.oscPort)) oscIn->addListener (this);
        else c.say ("OSC couldn't listen on port " + String (s.oscPort) + ": something else is using it.", true);
        oscConnected = oscOut.connect (String (s.oscReplyHost), s.oscReplyPort);
    }
}

//==============================================================================
std::vector<Id> Surfaces::strips() const
{
    std::vector<Id> visible;
    for (auto& t : c.project.tracks)
        if (! t.hidden && ! c.project.hiddenByFolder (t.id)) visible.push_back (t.id);
    std::vector<Id> out;
    for (int i = 0; i < 8; ++i)
        out.push_back (bank + i < (int) visible.size() ? visible[(size_t) (bank + i)] : 0);
    return out;
}

Track* Surfaces::stripTrack (int strip)
{
    if (strip == 8) return &c.project.master;
    const auto ids = strips();
    return strip >= 0 && strip < 8 ? c.project.track (ids[(size_t) strip]) : nullptr;
}

void Surfaces::send (const MidiMessage& m)
{
    if (sendToSurface) sendToSurface (m);
    else if (mackieOut) mackieOut->sendMessageNow (m);
}

void Surfaces::sendBytes (const std::uint8_t* data, int size)
{
    send (MidiMessage (data, size));
}

//==============================================================================
void Surfaces::mackie (const MidiMessage& m)
{
    if (m.getRawDataSize() > 3) return;
    std::uint8_t bytes[3] {};
    for (int i = 0; i < m.getRawDataSize(); ++i) bytes[i] = m.getRawData()[i];
    const auto in = mackie::decode (bytes);
    switch (in.kind)
    {
        case mackie::Kind::Fader:
        {
            auto* t = stripTrack (in.strip);
            if (! t) break;
            t->volumeDb = mackie::faderToDb (in.value);
            if (auto* f = c.engine.fader (t->id)) f->volumeDb = (float) t->volumeDb;
            c.dirty = true;
            if (! touched[(size_t) jlimit (0, 8, in.strip)]) c.refresh();   // a surface without touch sensing
            else c.viewChanged();
            c.touchedMix (t->id, AutoTarget::Kind::Volume, "Volume");
            break;
        }
        case mackie::Kind::FaderTouch:
        {
            const auto s = (size_t) jlimit (0, 8, in.strip);
            if (in.pressed) { touched[s] = true; touchBefore = c.project; }
            else
            {
                touched[s] = false;
                if (! (touchBefore == c.project)) c.history.push (touchBefore, "Fader");
                c.refresh();
            }
            break;
        }
        case mackie::Kind::VPot:
        {
            auto* t = stripTrack (in.strip);
            if (! t) break;
            t->pan = jlimit (-1.0, 1.0, t->pan + in.delta * 0.02);
            if (auto* f = c.engine.fader (t->id)) f->pan = (float) t->pan;
            c.dirty = true;
            c.viewChanged();
            break;
        }
        case mackie::Kind::Jog:
        {
            // A click of the wheel is a beat.
            c.locate (std::max<Tick> (0, c.playhead() + in.delta * c.project.tempo.meterAt (c.playhead()).beatTicks()));
            break;
        }
        case mackie::Kind::Button:
        {
            if (! in.pressed) break;
            const int b = in.button;
            auto* t = stripTrack (in.strip);
            const Id id = t ? t->id : 0;
            auto toggle = [this, id] (const char* label, bool Track::* field)
            {
                if (id) c.apply (label, [id, field] (Project& p) { if (auto* x = p.track (id)) x->*field = ! (x->*field); });
            };
            if (b >= note::recArm && b < note::recArm + 8) toggle ("Arm", &Track::armed);
            else if (b >= note::solo && b < note::solo + 8) toggle ("Solo", &Track::solo);
            else if (b >= note::mute && b < note::mute + 8) toggle ("Mute", &Track::mute);
            else if (b >= note::select && b < note::select + 8) { if (id) c.selectTracks ({ id }); }
            else if (b >= note::vpotPush && b < note::vpotPush + 8)
            {
                if (t) { t->pan = 0; if (auto* f = c.engine.fader (id)) f->pan = 0; c.refresh(); }
            }
            else if (b == note::bankLeft) bank = std::max (0, bank - 8);
            else if (b == note::bankRight) bank = std::min (std::max (0, (int) c.project.tracks.size() - 1), bank + 8);
            else if (b == note::channelLeft || b == note::left) bank = std::max (0, bank - 1);
            else if (b == note::channelRight || b == note::right) bank = std::min (std::max (0, (int) c.project.tracks.size() - 1), bank + 1);
            else if (b == note::play) c.play();
            else if (b == note::stop) c.stop();
            else if (b == note::record) c.record();
            else if (b == note::cycle) c.toggleLoop();
            else if (b == note::undo) c.undo();
            else if (b == note::save) c.save();
            else if (b == note::rewind || b == note::forward)
            {
                const int bar = c.project.tempo.barAt (c.playhead());
                c.locate (c.project.tempo.barStart (std::max (0, bar + (b == note::forward ? 1 : (c.playhead() > c.project.tempo.barStart (bar) ? 0 : -1)))));
            }
            else if (b == note::up || b == note::down)
            {
                // The track above or below the chosen one.
                const int i = c.project.indexOf (c.focusTrack) + (b == note::up ? -1 : 1);
                if (i >= 0 && i < (int) c.project.tracks.size()) c.selectTracks ({ c.project.tracks[(size_t) i].id });
            }
            feedback (true);
            break;
        }
        case mackie::Kind::None:
            break;
    }
}

void Surfaces::feedback (bool everything)
{
    if (! mackieOut && ! sendToSurface) return;
    const auto ids = strips();
    for (int s = 0; s < 9; ++s)
    {
        auto* t = stripTrack (s);
        auto& was = shown[(size_t) s];
        const int faderPos = t ? (int) std::lround (mackie::dbToFader (t->volumeDb) * 16383.0) : 0;
        if (! touched[(size_t) s] && (everything || faderPos != was.fader))
        {
            const auto f = mackie::fader (s, faderPos / 16383.0);
            sendBytes (f.data(), 3);
            was.fader = faderPos;
        }
        if (s == 8) continue;
        auto light = [&] (int& state, int base, bool on)
        {
            if (everything || state != (int) on)
            {
                const auto l = mackie::led (base + s, on);
                sendBytes (l.data(), 3);
                state = (int) on;
            }
        };
        light (was.arm, note::recArm, t && t->armed);
        light (was.solo, note::solo, t && t->solo);
        light (was.mute, note::mute, t && t->mute);
        light (was.select, note::select, t && t->id == c.focusTrack);
        const int pan = t ? (int) std::lround ((t->pan + 1.0) * 5.0) : -2;
        if (everything || pan != was.pan)
        {
            const auto r = mackie::panRing (s, t ? t->pan : 0.0);
            sendBytes (r.data(), 3);
            was.pan = pan;
        }
        const auto name = mackie::fit (t ? t->name : std::string());
        if (everything || name != was.name)
        {
            const auto d = mackie::display (s * 7, name);
            sendBytes (d.data(), (int) d.size());
            was.name = name;
        }
        const auto value = mackie::fit (t ? String (t->volumeDb, 1).toStdString() : std::string());
        if (everything || value != was.value)
        {
            const auto d = mackie::display (56 + s * 7, value);
            sendBytes (d.data(), (int) d.size());
            was.value = value;
        }
        if (t)
            if (auto* f = c.engine.fader (t->id))
            {
                const auto peak = Decibels::gainToDecibels (f->level.load(), -100.0f);
                const auto mm = mackie::meter (s, peak);
                if (everything || (int) mm[1] != was.meter) { sendBytes (mm.data(), 2); was.meter = (int) mm[1]; }
            }
    }
    auto light = [&] (int& state, int n, bool on)
    {
        if (everything || state != (int) on) { const auto l = mackie::led (n, on); sendBytes (l.data(), 3); state = (int) on; }
    };
    light (playShown, note::play, c.engine.isPlaying());
    light (recordShown, note::record, c.engine.isRecording());
    light (cycleShown, note::cycle, c.project.loop.on);
    // The timecode display shows bars, beats and sixteenths.
    const auto bb = c.project.tempo.barBeatAt (c.playhead());
    const auto sixteenth = (int) (bb.tick / (PPQ / 4)) + 1;
    const auto digits = String (bb.bar + 1).paddedLeft (' ', 3) + String (bb.beat + 1).paddedLeft ('0', 2)
                      + String (sixteenth).paddedLeft ('0', 2) + String ((int) (bb.tick % (PPQ / 4))).paddedLeft ('0', 3);
    if (everything || digits.toStdString() != timeShown)
    {
        for (auto& m : mackie::timecode (digits.toStdString())) sendBytes (m.data(), 3);
        timeShown = digits.toStdString();
    }
}

//==============================================================================
void Surfaces::osc (const OSCMessage& m)
{
    const auto address = m.getAddressPattern().toString();
    auto number = [&m] (double fallback) -> double
    {
        if (m.isEmpty()) return fallback;
        if (m[0].isFloat32()) return m[0].getFloat32();
        if (m[0].isInt32()) return m[0].getInt32();
        return fallback;
    };
    if (address == "/kalibre/play") { c.play(); return; }
    if (address == "/kalibre/stop") { c.stop(); return; }
    if (address == "/kalibre/record") { c.record(); return; }
    if (address == "/kalibre/undo") { c.undo(); return; }
    if (address == "/kalibre/redo") { c.redo(); return; }
    if (address == "/kalibre/loop") { if ((number (! c.project.loop.on) >= 0.5) != c.project.loop.on) c.toggleLoop(); return; }
    if (address == "/kalibre/tempo")
    {
        const double bpm = number (0);
        if (bpm >= TempoMap::minBpm && bpm <= TempoMap::maxBpm) c.apply ("Tempo", [bpm] (Project& p) { p.tempo.setTempoAt (0, bpm); });
        return;
    }
    if (address == "/kalibre/locate") { c.locate (c.project.tempo.barStart (std::max (0, (int) number (1) - 1))); return; }
    // /kalibre/track/3/volume: tracks counted from 1 as they're shown.
    if (address.startsWith ("/kalibre/track/"))
    {
        const auto rest = address.fromFirstOccurrenceOf ("/kalibre/track/", false, false);
        const int index = rest.upToFirstOccurrenceOf ("/", false, false).getIntValue() - 1;
        const auto what = rest.fromFirstOccurrenceOf ("/", false, false);
        if (index < 0 || index >= (int) c.project.tracks.size()) return;
        auto& t = c.project.tracks[(size_t) index];
        const Id id = t.id;
        if (what == "volume")
        {
            t.volumeDb = mackie::faderToDb (jlimit (0.0, 1.0, number (0)));
            c.refresh();
        }
        else if (what == "pan")
        {
            t.pan = jlimit (-1.0, 1.0, number (0));
            c.refresh();
        }
        else if (what == "mute" || what == "solo" || what == "arm")
        {
            const bool on = number (1) >= 0.5;
            bool Track::* field = what == "mute" ? &Track::mute : what == "solo" ? &Track::solo : &Track::armed;
            c.apply (what == "mute" ? "Mute" : what == "solo" ? "Solo" : "Arm", [id, field, on] (Project& p) { if (auto* x = p.track (id)) x->*field = on; });
        }
        else if (what == "select") c.selectTracks ({ id });
    }
}

void Surfaces::oscFeedback()
{
    if (! oscConnected) return;
    auto sendIfChanged = [this] (const String& address, const OSCArgument& value, const String& key)
    {
        if (oscShown[address] == key) return;
        oscShown[address] = key;
        oscOut.send (OSCMessage (OSCAddressPattern (address), value));
    };
    sendIfChanged ("/kalibre/playing", OSCArgument ((int32) c.engine.isPlaying()), String ((int) c.engine.isPlaying()));
    sendIfChanged ("/kalibre/recording", OSCArgument ((int32) c.engine.isRecording()), String ((int) c.engine.isRecording()));
    const auto pos = String (c.project.tempo.formatBarBeat (c.playhead()));
    sendIfChanged ("/kalibre/position", OSCArgument (pos), pos);
    const auto bpm = (float) c.project.tempo.bpmAt (c.playhead());
    sendIfChanged ("/kalibre/tempo", OSCArgument (bpm), String (bpm, 2));
    for (size_t i = 0; i < c.project.tracks.size() && i < 32; ++i)
    {
        const auto& t = c.project.tracks[i];
        const String base = "/kalibre/track/" + String ((int) i + 1) + "/";
        sendIfChanged (base + "name", OSCArgument (String (t.name)), String (t.name));
        sendIfChanged (base + "volume", OSCArgument ((float) mackie::dbToFader (t.volumeDb)), String (t.volumeDb, 2));
        sendIfChanged (base + "pan", OSCArgument ((float) t.pan), String (t.pan, 2));
        sendIfChanged (base + "mute", OSCArgument ((int32) t.mute), String ((int) t.mute));
        sendIfChanged (base + "solo", OSCArgument ((int32) t.solo), String ((int) t.solo));
    }
}

void Surfaces::timerCallback()
{
    feedback (false);
    oscFeedback();
}

} // namespace kal
