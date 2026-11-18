#include "DawProject.h"

#include "Xml.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>

namespace kal::dawproject
{
namespace
{
std::string num (double v)
{
    char buf[48];
    std::snprintf (buf, sizeof buf, "%.6f", v);
    std::string s (buf);
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (! s.empty() && s.back() == '.') s.pop_back();
    return s;
}

std::string beats (Tick t) { return num ((double) t / PPQ); }

std::string colourText (std::uint32_t argb)
{
    char buf[16];
    std::snprintf (buf, sizeof buf, "#%02x%02x%02x", (argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff);
    return buf;
}

std::uint32_t colourFrom (const std::string& s)
{
    if (s.size() < 7 || s[0] != '#') return 0;
    return 0xff000000u | (std::uint32_t) std::strtoul (s.substr (1, 6).c_str(), nullptr, 16);
}

double linearFromDb (double db) { return db <= -150 ? 0.0 : std::pow (10.0, db / 20.0); }
double dbFromLinear (double v) { return v <= 0.0000001 ? -150.0 : 20.0 * std::log10 (v); }

std::string fileName (const std::string& path)
{
    const auto slash = path.find_last_of ("/\\");
    return slash == std::string::npos ? path : path.substr (slash + 1);
}

const char* roleName (DeviceRole r) { return r == DeviceRole::MidiFx ? "noteFX" : r == DeviceRole::Instrument ? "instrument" : "audioFX"; }

struct Writer
{
    const Project& p;
    xml::Writer w { "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n" };
    int next = 0;
    std::map<Id, std::string> channelOf, trackOf;
    Written out;
    std::set<std::string> usedAudio;

    explicit Writer (const Project& pr) : p (pr) {}

    std::string id() { return "id" + std::to_string (next++); }

    void channel (const Track& t, const std::string& role)
    {
        const auto cid = channelOf[t.id];
        xml::Writer::Attributes a { { "audioChannels", "2" }, { "role", role }, { "solo", t.solo ? "true" : "false" }, { "id", cid } };
        if (t.kind != TrackKind::Master)
        {
            Id dest = t.output != 0 ? t.output : t.parent;
            a.push_back ({ "destination", dest != 0 && channelOf.count (dest) ? channelOf[dest] : channelOf[p.master.id] });
        }
        w.open ("Channel", a);
        if (! t.devices.empty())
        {
            w.open ("Devices");
            for (auto& d : t.devices) device (d);
            w.close();
        }
        w.empty ("Mute", { { "value", t.mute ? "true" : "false" }, { "id", id() }, { "name", "Mute" } });
        w.empty ("Pan", { { "max", "1" }, { "min", "0" }, { "unit", "normalized" }, { "value", num ((t.pan + 1.0) / 2.0) }, { "id", id() }, { "name", "Pan" } });
        if (! t.sends.empty())
        {
            w.open ("Sends");
            for (auto& s : t.sends)
            {
                if (! channelOf.count (s.target)) continue;
                w.open ("Send", { { "destination", channelOf[s.target] }, { "type", s.preFader ? "pre" : "post" }, { "id", id() } });
                w.empty ("Enable", { { "value", s.muted ? "false" : "true" }, { "id", id() }, { "name", "On/Off" } });
                w.empty ("Volume", { { "max", "2" }, { "min", "0" }, { "unit", "linear" }, { "value", num (linearFromDb (s.gainDb)) }, { "id", id() }, { "name", "Send" } });
                w.close();
            }
            w.close();
        }
        // The schema's order: Devices, Mute, Pan, Sends, Volume.
        w.empty ("Volume", { { "max", "2" }, { "min", "0" }, { "unit", "linear" }, { "value", num (linearFromDb (t.volumeDb)) }, { "id", id() }, { "name", "Volume" } });
        w.close();
    }

    void device (const Device& d)
    {
        const auto& r = d.plugin;
        const std::string element = r.format == "VST3" ? "Vst3Plugin" : r.format == "AudioUnit" ? "AuPlugin"
                                  : r.format == "CLAP" ? "ClapPlugin" : "BuiltinDevice";
        const auto did = id();
        w.open (element, { { "deviceID", r.identifier }, { "deviceName", r.name }, { "deviceRole", roleName (d.role) },
                           { "deviceVendor", r.format == "Kalibre" ? std::string ("Kalibre") : r.manufacturer },
                           { "loaded", d.active ? "true" : "false" }, { "id", did }, { "name", r.name } });
        w.empty ("Enabled", { { "value", d.bypassed ? "false" : "true" }, { "id", id() }, { "name", "On/Off" } });
        if (! d.state.empty())
        {
            if (r.format == "Kalibre")
            {
                const auto path = "plugins/" + did + ".kalibre-state";
                w.empty ("State", { { "path", path } });
                out.states.push_back ({ path, d.state });
            }
            else out.states.push_back ({ "kalibre/states/" + did + ".bin", d.state });   // only Kalibre reads it back
        }
        w.close();
    }

    void track (const Track& t)
    {
        const bool folder = t.isFolder();
        const std::string content = folder ? "tracks" : t.kind == TrackKind::Return ? "audio" : "audio notes";
        w.open ("Track", { { "contentType", content }, { "loaded", "true" }, { "id", trackOf[t.id] }, { "name", t.name }, { "color", colourText (t.colour) } });
        channel (t, folder ? "submix" : t.kind == TrackKind::Return ? "effect" : "regular");
        for (auto& child : p.tracks)
            if (child.parent == t.id) track (child);
        w.close();
    }

    void clip (const Clip& c)
    {
        xml::Writer::Attributes a { { "time", beats (c.start) }, { "duration", beats (c.length) }, { "name", c.name } };
        if (c.colour) a.push_back ({ "color", colourText (c.colour) });
        if (c.isMidi())
        {
            a.push_back ({ "playStart", beats (c.offset) });
            if (c.loopLength > 0) { a.push_back ({ "loopStart", "0" }); a.push_back ({ "loopEnd", beats (c.loopLength) }); }
            w.open ("Clip", a);
            const bool controllers = std::any_of (c.midi.events.begin(), c.midi.events.end(), [] (const MidiEvent& e) { return e.type == EventType::Controller; });
            if (controllers) w.open ("Lanes", { { "id", id() } });
            w.open ("Notes", { { "id", id() } });
            for (auto& n : c.midi.notes)
                w.empty ("Note", { { "time", beats (n.start) }, { "duration", beats (n.length) }, { "channel", std::to_string (n.channel & 15) },
                                   { "key", std::to_string (n.pitch) }, { "vel", num (n.velocity / 127.0) }, { "rel", num (n.velocity / 127.0) } });
            w.close();
            if (controllers)
            {
                std::map<std::pair<int, int>, std::vector<const MidiEvent*>> lanes;
                for (auto& e : c.midi.events)
                    if (e.type == EventType::Controller) lanes[{ e.channel, e.number }].push_back (&e);
                for (auto& [key, events] : lanes)
                {
                    w.open ("Points", { { "unit", "normalized" }, { "id", id() } });
                    w.empty ("Target", { { "expression", "channelController" }, { "channel", std::to_string (key.first) }, { "controller", std::to_string (key.second) } });
                    for (auto* e : events) w.empty ("RealPoint", { { "time", beats (e->tick) }, { "value", num (e->value / 127.0) }, { "interpolation", "hold" } });
                    w.close();
                }
                w.close();
            }
            w.close();
            return;
        }
        // Audio: where in the file is in seconds.
        a.push_back ({ "contentTimeUnit", "seconds" });
        a.push_back ({ "playStart", num (c.audio.sourceStart) });
        a.push_back ({ "fadeTimeUnit", "seconds" });
        a.push_back ({ "fadeInTime", num (c.audio.fadeIn) });
        a.push_back ({ "fadeOutTime", num (c.audio.fadeOut) });
        w.open ("Clip", a);
        std::string name = fileName (c.audio.file);
        std::string path = "audio/" + name;
        for (int k = 2; usedAudio.count (path) && out.audio.end() == std::find_if (out.audio.begin(), out.audio.end(), [&] (auto& x) { return x.first == path && x.second == c.audio.file; }); ++k)
            path = "audio/" + std::to_string (k) + " " + name;
        if (! usedAudio.count (path)) { usedAudio.insert (path); out.audio.push_back ({ path, c.audio.file }); }
        w.open ("Audio", { { "channels", std::to_string (c.audio.fileChannels) }, { "duration", num (c.audio.sourceStart + c.audio.sourceLength) },
                           { "sampleRate", num (c.audio.fileSampleRate) }, { "algorithm", "stretch" }, { "id", id() } });
        w.empty ("File", { { "path", path } });
        w.close();
        w.close();
    }
};
} // namespace

Written write (const Project& p, const std::string& appVersion)
{
    Writer wr (p);
    auto name = [&] (const Track& t) { wr.trackOf[t.id] = wr.id(); wr.channelOf[t.id] = wr.id(); };
    for (auto& t : p.tracks) name (t);
    name (p.master);
    auto& w = wr.w;
    w.open ("Project", { { "version", "1.0" } });
    w.empty ("Application", { { "name", "Kalibre" }, { "version", appVersion } });
    w.open ("Transport");
    const auto tempoId = wr.id(), meterId = wr.id();
    w.empty ("Tempo", { { "max", "666" }, { "min", "20" }, { "unit", "bpm" }, { "value", num (p.tempo.tempos().front().bpm) }, { "id", tempoId }, { "name", "Tempo" } });
    const auto& m0 = p.tempo.meters().front();
    w.empty ("TimeSignature", { { "denominator", std::to_string (m0.denominator) }, { "numerator", std::to_string (m0.numerator) }, { "id", meterId } });
    w.close();
    w.open ("Structure");
    for (auto& t : p.tracks)
        if (t.parent == 0 && t.kind != TrackKind::Cue) wr.track (t);
    w.open ("Track", { { "contentType", "audio notes" }, { "loaded", "true" }, { "id", wr.trackOf[p.master.id] }, { "name", "Master" }, { "color", colourText (p.master.colour) } });
    wr.channel (p.master, "master");
    w.close();
    w.close();

    w.open ("Arrangement", { { "id", wr.id() } });
    w.open ("Lanes", { { "timeUnit", "beats" }, { "id", wr.id() } });
    for (auto& t : p.tracks)
    {
        if (! t.holdsClips() || t.clips().empty()) continue;
        w.open ("Lanes", { { "track", wr.trackOf[t.id] }, { "id", wr.id() } });
        w.open ("Clips", { { "id", wr.id() } });
        for (auto& c : t.clips()) wr.clip (c);
        w.close();
        w.close();
    }
    w.close();
    if (! p.markers.empty())
    {
        w.open ("Markers", { { "id", wr.id() } });
        for (auto& m : p.markers) w.empty ("Marker", { { "time", beats (m.tick) }, { "name", m.name } });
        w.close();
    }
    if (p.tempo.tempos().size() > 1)
    {
        w.open ("TempoAutomation", { { "unit", "bpm" }, { "id", wr.id() } });
        w.empty ("Target", { { "parameter", tempoId } });
        for (auto& t : p.tempo.tempos())
            w.empty ("RealPoint", { { "time", beats (t.tick) }, { "value", num (t.bpm) }, { "interpolation", t.rampToNext ? "linear" : "hold" } });
        w.close();
    }
    if (p.tempo.meters().size() > 1)
    {
        w.open ("TimeSignatureAutomation", { { "id", wr.id() } });
        w.empty ("Target", { { "parameter", meterId } });
        for (auto& m : p.tempo.meters())
            w.empty ("TimeSignaturePoint", { { "time", beats (p.tempo.barStart (m.bar)) }, { "numerator", std::to_string (m.numerator) }, { "denominator", std::to_string (m.denominator) } });
        w.close();
    }
    w.close();
    w.empty ("Scenes");
    w.close();
    wr.out.projectXml = w.str();

    xml::Writer meta { "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n" };
    meta.open ("MetaData");
    const auto& md = p.metadata;
    meta.leaf ("Title", md.title.empty() ? p.name : md.title);
    if (! md.artist.empty()) meta.leaf ("Artist", md.artist);
    if (! md.album.empty()) meta.leaf ("Album", md.album);
    if (! md.genre.empty()) meta.leaf ("Genre", md.genre);
    if (md.year > 0) meta.leaf ("Year", std::to_string (md.year));
    if (! md.comment.empty()) meta.leaf ("Comment", md.comment);
    meta.close();
    wr.out.metadataXml = meta.str();
    return wr.out;
}

//==============================================================================
namespace
{
double numAttr (const xml::Node& n, const std::string& a, double fallback)
{
    const auto s = n.attr (a);
    if (s.empty()) return fallback;
    char* end = nullptr;
    const double v = std::strtod (s.c_str(), &end);
    return end == s.c_str() ? fallback : v;
}

const xml::Node* firstChild (const xml::Node& n, std::initializer_list<const char*> names)
{
    for (auto& c : n.children)
        for (auto* nm : names) if (c.name == nm) return &c;
    return nullptr;
}

struct Reader
{
    Read r;
    Project& p = r.project;
    std::map<std::string, Id> track, channel;   // xml id -> Kalibre track (the master is p.master.id)
    std::map<Id, std::string> wantsOutput;      // track -> destination channel's xml id
    std::vector<std::pair<Id, const xml::Node*>> sends;
    std::set<std::string> leftOut;
    bool seconds = false;

    Tick toTick (double v, bool inSeconds) const
    {
        return inSeconds ? p.tempo.tickAt (v) : (Tick) std::llround (v * PPQ);
    }

    void readChannel (const xml::Node& ch, Track& t)
    {
        if (auto* v = ch.child ("Volume")) t.volumeDb = std::clamp (dbFromLinear (numAttr (*v, "value", 1.0)), -150.0, 12.0);
        if (auto* v = ch.child ("Pan")) t.pan = std::clamp (numAttr (*v, "value", 0.5) * 2.0 - 1.0, -1.0, 1.0);
        if (auto* v = ch.child ("Mute")) t.mute = v->attr ("value") == "true";
        t.solo = ch.attr ("solo") == "true";
        if (auto* devs = ch.child ("Devices"))
            for (auto& d : devs->children)
            {
                Device dev;
                dev.id = p.newId();
                const auto role = d.attr ("deviceRole");
                dev.role = role == "noteFX" ? DeviceRole::MidiFx : role == "instrument" ? DeviceRole::Instrument : DeviceRole::AudioFx;
                auto& ref = dev.plugin;
                ref.name = d.attr ("deviceName", d.attr ("name"));
                ref.manufacturer = d.attr ("deviceVendor");
                ref.identifier = d.attr ("deviceID");
                ref.isInstrument = dev.role == DeviceRole::Instrument;
                if (d.name == "Vst3Plugin") ref.format = "VST3";
                else if (d.name == "AuPlugin") ref.format = "AudioUnit";
                else if (d.name == "ClapPlugin") ref.format = "CLAP";
                else if (ref.manufacturer == "Kalibre") ref.format = "Kalibre";
                else { leftOut.insert ("another program's own devices (" + (ref.name.empty() ? d.name : ref.name) + ")"); continue; }
                if (auto* en = d.child ("Enabled")) dev.bypassed = en->attr ("value") == "false";
                dev.active = d.attr ("loaded") != "false";
                if (auto* st = d.child ("State"); st && ! st->attr ("path").empty())
                {
                    if (ref.format == "Kalibre") r.states[dev.id] = st->attr ("path");
                    else leftOut.insert ("other programs' plug-in settings (the plug-ins come in as they start)");
                }
                r.kalibreStates[dev.id] = "kalibre/states/" + d.attr ("id") + ".bin";
                t.devices.push_back (dev);
            }
        if (auto* s = ch.child ("Sends"))
            for (auto& send : s->children) if (send.name == "Send") sends.push_back ({ t.id, &send });
    }

    void readTrack (const xml::Node& n, Id parent)
    {
        const auto* ch = n.child ("Channel");
        const auto role = ch ? ch->attr ("role") : std::string();
        const bool hasChildren = n.child ("Track") != nullptr;
        if (role == "master")
        {
            p.master.name = "Master";
            if (ch) { channel[ch->attr ("id")] = p.master.id; readChannel (*ch, p.master); }
            track[n.attr ("id")] = p.master.id;
            return;
        }
        Track t;
        t.id = p.newId();
        t.name = n.attr ("name", "Track");
        t.kind = hasChildren || n.attr ("contentType") == "tracks" ? TrackKind::Folder : role == "effect" ? TrackKind::Return : TrackKind::Track;
        t.parent = parent;
        if (auto c = colourFrom (n.attr ("color")); c) t.colour = c;
        p.tracks.push_back (t);
        const Id id = t.id;
        track[n.attr ("id")] = id;
        if (ch)
        {
            channel[ch->attr ("id")] = id;
            readChannel (*ch, *p.track (id));
            if (! ch->attr ("destination").empty()) wantsOutput[id] = ch->attr ("destination");
        }
        for (auto& child : n.children)
            if (child.name == "Track") readTrack (child, id);
    }

    void readNotes (const xml::Node& notes, Clip& c, double scale)
    {
        for (auto& e : notes.children)
        {
            if (e.name != "Note") continue;
            Note n;
            n.id = p.newId();
            n.start = (Tick) std::llround (numAttr (e, "time", 0) * scale);
            n.length = std::max<Tick> (1, (Tick) std::llround (numAttr (e, "duration", 0.25) * scale));
            n.pitch = std::clamp ((int) numAttr (e, "key", 60), 0, 127);
            n.channel = std::clamp ((int) numAttr (e, "channel", 0), 0, 15);
            n.velocity = std::clamp ((int) std::lround (numAttr (e, "vel", 0.8) * 127.0), 1, 127);
            c.midi.notes.push_back (n);
        }
    }

    void readPoints (const xml::Node& pts, Clip& c, double scale)
    {
        const auto* target = pts.child ("Target");
        if (! target || target->attr ("expression") != "channelController") { leftOut.insert ("automation inside clips other than controllers"); return; }
        const int cc = std::clamp ((int) numAttr (*target, "controller", 1), 0, 127);
        const int chn = std::clamp ((int) numAttr (*target, "channel", 0), 0, 15);
        for (auto& e : pts.children)
            if (e.name == "RealPoint")
            {
                MidiEvent ev;
                ev.tick = (Tick) std::llround (numAttr (e, "time", 0) * scale);
                ev.type = EventType::Controller;
                ev.channel = chn;
                ev.number = cc;
                ev.value = std::clamp ((int) std::lround (numAttr (e, "value", 0) * 127.0), 0, 127);
                c.midi.events.push_back (ev);
            }
    }

    // One <Clip> on a track; `offsetTicks` for clips inside clips.
    void readClip (const xml::Node& n, Id trackId, Tick offsetTicks)
    {
        Clip c;
        c.start = offsetTicks + toTick (numAttr (n, "time", 0), seconds);
        const Tick end = offsetTicks + toTick (numAttr (n, "time", 0) + numAttr (n, "duration", 4), seconds);
        c.length = std::max<Tick> (1, end - c.start);
        c.name = n.attr ("name");
        if (auto col = colourFrom (n.attr ("color")); col) c.colour = col;
        const bool contentSeconds = n.attr ("contentTimeUnit") == "seconds";
        // Nested clips: each its own, at its own place.
        if (auto* inner = n.child ("Clips"))
        {
            for (auto& x : inner->children) if (x.name == "Clip") readClip (x, trackId, c.start - (Tick) std::llround (numAttr (n, "playStart", 0) * PPQ));
            return;
        }
        const xml::Node* audio = n.child ("Audio");
        if (! audio)
            if (auto* warps = n.child ("Warps"))
            {
                audio = warps->child ("Audio");
                if (warps->all ("Warp").size() > 2) leftOut.insert ("warping");
            }
        if (audio)
        {
            c.kind = Clip::Kind::Audio;
            if (auto* f = audio->child ("File")) c.audio.file = f->attr ("path");
            c.audio.fileSampleRate = numAttr (*audio, "sampleRate", 48000);
            c.audio.fileChannels = (int) numAttr (*audio, "channels", 2);
            c.audio.sourceStart = contentSeconds ? numAttr (n, "playStart", 0) : 0;
            c.audio.sourceLength = p.tempo.secondsAt (end) - p.tempo.secondsAt (c.start);
            const bool fadeSeconds = n.attr ("fadeTimeUnit") != "beats";
            auto fade = [&] (const char* a) { const double v = numAttr (n, a, 0); return fadeSeconds ? v : v * 60.0 / p.tempo.bpmAt (c.start); };
            c.audio.fadeIn = fade ("fadeInTime");
            c.audio.fadeOut = fade ("fadeOutTime");
            c.id = p.newId();
            p.track (trackId)->clips().push_back (c);
            return;
        }
        c.kind = Clip::Kind::Midi;
        const double scale = PPQ;
        c.offset = (Tick) std::llround (numAttr (n, "playStart", 0) * scale);
        if (! n.attr ("loopEnd").empty())
        {
            const Tick ls = (Tick) std::llround (numAttr (n, "loopStart", 0) * scale), le = (Tick) std::llround (numAttr (n, "loopEnd", 0) * scale);
            if (le > ls && ls == 0) c.loopLength = le;
            else if (le > ls) leftOut.insert ("loops that don't start at a clip's beginning");
        }
        if (auto* notes = n.child ("Notes")) readNotes (*notes, c, scale);
        if (auto* lanes = n.child ("Lanes"))
            for (auto& x : lanes->children)
            {
                if (x.name == "Notes") readNotes (x, c, scale);
                else if (x.name == "Points") readPoints (x, c, scale);
            }
        std::sort (c.midi.notes.begin(), c.midi.notes.end(), [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
        std::sort (c.midi.events.begin(), c.midi.events.end(), [] (const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
        c.id = p.newId();
        p.track (trackId)->clips().push_back (c);
    }

    void readLanes (const xml::Node& lanes, Id trackId)
    {
        const bool before = seconds;
        if (! lanes.attr ("timeUnit").empty()) seconds = lanes.attr ("timeUnit") == "seconds";
        if (auto it = track.find (lanes.attr ("track")); it != track.end()) trackId = it->second;
        for (auto& x : lanes.children)
        {
            if (x.name == "Lanes") readLanes (x, trackId);
            else if (x.name == "Clips")
            {
                auto* t = p.track (trackId);
                if (! t || ! t->holdsClips()) { leftOut.insert ("clips on folders or the master"); continue; }
                for (auto& c : x.children) if (c.name == "Clip") readClip (c, trackId, 0);
            }
            else if (x.name == "Points") leftOut.insert ("automation");
        }
        seconds = before;
    }
};
} // namespace

Read read (const std::string& text, const std::string& metadata)
{
    Reader rd;
    auto parsed = xml::parse (text);
    if (! parsed.root || parsed.root->name != "Project")
    {
        rd.r.error = parsed.root ? "This isn't a DAWproject: its project.xml has no Project." : "The DAWproject's project.xml couldn't be read: " + parsed.error;
        return rd.r;
    }
    const auto& root = *parsed.root;
    auto& p = rd.p;
    p.tracks.clear();
    if (auto* app = root.child ("Application")) p.name = "From " + app->attr ("name", "another program");
    if (auto* tr = root.child ("Transport"))
    {
        if (auto* t = tr->child ("Tempo")) p.tempo.setTempoAt (0, std::clamp (numAttr (*t, "value", 120), TempoMap::minBpm, TempoMap::maxBpm));
        if (auto* ts = tr->child ("TimeSignature")) p.tempo.setMeterAt (0, (int) numAttr (*ts, "numerator", 4), (int) numAttr (*ts, "denominator", 4));
    }
    if (auto* arr = root.child ("Arrangement"))
    {
        // Time signatures first: bars depend on them, and tempos on the bars' ticks.
        if (auto* tsa = arr->child ("TimeSignatureAutomation"))
            for (auto& pt : tsa->children)
            {
                const Tick at = (Tick) std::llround (numAttr (pt, "time", 0) * PPQ);
                p.tempo.setMeterAt (p.tempo.barAt (at), (int) numAttr (pt, "numerator", 4), (int) numAttr (pt, "denominator", 4));
            }
        if (auto* ta = arr->child ("TempoAutomation"))
            for (auto& pt : ta->children)
                if (pt.name == "RealPoint")
                    p.tempo.setTempoAt ((Tick) std::llround (numAttr (pt, "time", 0) * PPQ), std::clamp (numAttr (pt, "value", 120), TempoMap::minBpm, TempoMap::maxBpm),
                                        pt.attr ("interpolation") == "linear");
    }
    if (auto* st = root.child ("Structure"))
        for (auto& t : st->children) if (t.name == "Track") rd.readTrack (t, 0);
    // Outputs and sends, now every channel is known.
    for (auto& [tid, dest] : rd.wantsOutput)
    {
        auto it = rd.channel.find (dest);
        auto* t = p.track (tid);
        if (! t || it == rd.channel.end() || it->second == p.master.id || it->second == t->parent) continue;
        t->output = it->second;
    }
    for (auto& [tid, node] : rd.sends)
    {
        auto it = rd.channel.find (node->attr ("destination"));
        if (it == rd.channel.end() || ! p.track (it->second)) continue;
        Send s;
        s.target = it->second;
        if (auto* v = node->child ("Volume")) s.gainDb = std::clamp (dbFromLinear (numAttr (*v, "value", 0.5)), -150.0, 12.0);
        if (auto* en = node->child ("Enable")) s.muted = en->attr ("value") == "false";
        s.preFader = node->attr ("type") == "pre";
        if (auto* t = (tid == p.master.id ? &p.master : p.track (tid))) t->sends.push_back (s);
    }
    if (auto* arr = root.child ("Arrangement"))
    {
        if (auto* lanes = arr->child ("Lanes")) rd.readLanes (*lanes, 0);
        if (auto* markers = arr->child ("Markers"))
            for (auto& m : markers->children)
                if (m.name == "Marker") p.markers.push_back ({ p.newId(), rd.toTick (numAttr (m, "time", 0), false), m.attr ("name") });
    }
    for (auto& t : p.tracks)
    {
        std::sort (t.clips().begin(), t.clips().end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
        if (t.isFolder() && ! t.clips().empty()) t.clips().clear();
    }
    if (! metadata.empty())
        if (auto meta = xml::parse (metadata); meta.root)
        {
            auto& md = p.metadata;
            md.title = meta.root->childText ("Title");
            md.artist = meta.root->childText ("Artist");
            md.album = meta.root->childText ("Album");
            md.genre = meta.root->childText ("Genre");
            md.comment = meta.root->childText ("Comment");
            md.year = (int) meta.root->childNumber ("Year", 0);
            if (! md.title.empty()) p.name = md.title;
        }
    for (auto& s : rd.leftOut) rd.r.leftOut.push_back (s);
    rd.r.ok = true;
    return rd.r;
}

} // namespace kal::dawproject
