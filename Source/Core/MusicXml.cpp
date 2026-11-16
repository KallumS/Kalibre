#include "MusicXml.h"

#include "Notation.h"
#include "ScaleModel.h"
#include "Xml.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>

namespace kal::musicxml
{

namespace
{
constexpr const char* stepNames[] { "C", "D", "E", "F", "G", "A", "B" };
constexpr int stepPitches[] { 0, 2, 4, 5, 7, 9, 11 };
constexpr Tick grid = PPQ / 8;          // a 1/32: what the page is squared to
constexpr int divisions = 8;            // per quarter: one a 1/32
constexpr int maxVoices = 4;

int toDivisions (Tick t) { return static_cast<int> (t / grid); }
Tick snap (Tick t) { return (t + grid / 2) / grid * grid; }

const char* typeName (notation::Value v)
{
    switch (v)
    {
        case notation::Value::Whole:        return "whole";
        case notation::Value::Half:         return "half";
        case notation::Value::Quarter:      return "quarter";
        case notation::Value::Eighth:       return "eighth";
        case notation::Value::Sixteenth:    return "16th";
        case notation::Value::ThirtySecond: return "32nd";
    }
    return "quarter";
}

// The notes a track plays, in song ticks, from its MIDI clips as they play.
std::vector<Note> notesOf (const Track& t)
{
    std::vector<Note> out;
    for (const auto& c : t.clips())
    {
        if (! c.isMidi() || c.muted) continue;
        const Tick content = c.loopLength > 0 ? c.loopLength : std::numeric_limits<Tick>::max() / 4;
        for (Tick base = c.start - c.offset; base < c.end(); base += content)
            for (const auto& n : c.midi.notes)
            {
                if (c.loopLength > 0 && n.start >= c.loopLength) continue;
                Tick a = base + n.start, b = a + n.length;
                if (a < c.start || a >= c.end()) continue;
                b = std::min (b, c.end());
                if (c.loopLength > 0) b = std::min (b, base + c.loopLength);
                if (b <= a) continue;
                Note x = n;
                x.start = a;
                x.length = b - a;
                out.push_back (x);
            }
    }
    return out;
}

struct Chord { Tick start = 0, end = 0; std::vector<int> pitches; int velocity = 0; };

// Notes squared to the grid, gathered into chords, the chords into voices
// that never overlap themselves.
std::vector<std::vector<Chord>> voicesOf (const std::vector<Note>& notes)
{
    std::map<std::pair<Tick, Tick>, Chord> chords;
    for (auto& n : notes)
    {
        const Tick a = snap (n.start);
        Tick b = snap (n.end());
        if (b <= a) b = a + grid;
        auto& c = chords[{ a, b }];
        c.start = a;
        c.end = b;
        c.pitches.push_back (n.pitch);
        c.velocity = std::max (c.velocity, n.velocity);
    }
    std::vector<std::vector<Chord>> voices;
    std::vector<Tick> freeFrom;
    for (auto& [key, c] : chords)
    {
        std::sort (c.pitches.begin(), c.pitches.end());
        c.pitches.erase (std::unique (c.pitches.begin(), c.pitches.end()), c.pitches.end());
        size_t v = 0;
        while (v < voices.size() && freeFrom[v] > c.start) ++v;
        if (v == voices.size())
        {
            if (voices.size() < (size_t) maxVoices) { voices.emplace_back(); freeFrom.push_back (0); }
            else
            {
                // More layers than a staff can show: it waits for the voice that frees soonest.
                v = static_cast<size_t> (std::min_element (freeFrom.begin(), freeFrom.end()) - freeFrom.begin());
                if (freeFrom[v] >= c.end) continue;
                c.start = freeFrom[v];
            }
        }
        voices[v].push_back (c);
        freeFrom[v] = c.end;
    }
    return voices;
}

void writePitch (xml::Writer& w, int pitch, const ScaleChoice& scale)
{
    const auto s = notation::spell (pitch, scale);
    w.open ("pitch");
    w.leaf ("step", stepNames[s.letter]);
    if (s.accidental != 0) w.leaf ("alter", (long long) s.accidental);
    w.leaf ("octave", (long long) s.octave);
    w.close();
}

// A stretch as note values (longest first), tied together, and tied in or on.
void writeNotes (xml::Writer& w, Tick length, const Chord* chord, int voice, bool tieIn, bool tieOut, const ScaleChoice& scale)
{
    const auto parts = notation::split (length);
    for (size_t i = 0; i < parts.size(); ++i)
    {
        const bool stop = chord && (i > 0 || tieIn), start = chord && (i + 1 < parts.size() || tieOut);
        const size_t count = chord ? chord->pitches.size() : 1;
        for (size_t k = 0; k < count; ++k)
        {
            w.open ("note", chord ? xml::Writer::Attributes { { "dynamics", std::to_string ((int) std::lround (chord->velocity / 0.9)) } } : xml::Writer::Attributes {});
            if (k > 0) w.empty ("chord");
            if (chord) writePitch (w, chord->pitches[k], scale);
            else w.empty ("rest");
            w.leaf ("duration", (long long) toDivisions (parts[i].ticks));
            if (stop) w.empty ("tie", { { "type", "stop" } });
            if (start) w.empty ("tie", { { "type", "start" } });
            w.leaf ("voice", (long long) voice + 1);
            w.leaf ("type", typeName (parts[i].value));
            if (parts[i].dotted) w.empty ("dot");
            if (stop || start)
            {
                w.open ("notations");
                if (stop) w.empty ("tied", { { "type", "stop" } });
                if (start) w.empty ("tied", { { "type", "start" } });
                w.close();
            }
            w.close();
        }
    }
}

int letterOf (const std::string& step)
{
    for (int i = 0; i < 7; ++i) if (step == stepNames[i]) return i;
    return 0;
}
} // namespace

//==============================================================================
int fifthsFor (const ScaleChoice& s)
{
    const auto& roots = scaleview::roots;
    const auto& root = roots[(size_t) std::clamp (s.root, 0, (int) roots.size() - 1)];
    // Fifths of each letter's major key: C 0, D 2, E 4, F -1, G 1, A 3, B 5; each sharp adds 7.
    constexpr int letterFifths[] { 0, 2, 4, -1, 1, 3, 5 };
    int f = letterFifths[root.letter] + 7 * root.accidental;
    if (isMinor (s)) f -= 3;
    while (f > 7) f -= 12;
    while (f < -7) f += 12;
    return f;
}

bool isMinor (const ScaleChoice& s)
{
    const auto& scales = scaleview::scales;
    const auto& sc = scales[(size_t) std::clamp (s.scale, 0, (int) scales.size() - 1)];
    return std::find (sc.intervals.begin(), sc.intervals.end(), 3) != sc.intervals.end()
        && std::find (sc.intervals.begin(), sc.intervals.end(), 4) == sc.intervals.end();
}

ScaleChoice scaleFor (int fifths, bool minor)
{
    // The tonic of a major key with this many fifths; a minor key's is the major key's three fifths on.
    const int f = std::clamp (fifths + (minor ? 3 : 0), -7, 10);
    constexpr int fifthsOrder[] { 3, 0, 4, 1, 5, 2, 6 };   // F C G D A E B, as letter indices
    const int at = f + 1;
    const int letter = fifthsOrder[((at % 7) + 7) % 7];
    const int accidental = at >= 0 ? at / 7 : -((-at + 6) / 7);
    ScaleChoice out;
    out.scale = minor ? 1 : 0;
    for (size_t i = 0; i < scaleview::roots.size(); ++i)
        if (scaleview::roots[i].letter == letter && scaleview::roots[i].accidental == accidental) out.root = (int) i;
    return out;
}

//==============================================================================
std::string write (const Project& p, const std::vector<Id>& ids)
{
    xml::Writer w ("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n"
                   "<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML 4.0 Partwise//EN\" \"http://www.musicxml.org/dtds/partwise.dtd\">\n");
    w.open ("score-partwise", { { "version", "4.0" } });
    const std::string title = ! p.metadata.title.empty() ? p.metadata.title : p.name;
    w.open ("work");
    w.leaf ("work-title", title);
    w.close();
    w.open ("identification");
    if (! p.metadata.artist.empty()) w.leaf ("creator", p.metadata.artist, { { "type", "composer" } });
    w.open ("encoding");
    w.leaf ("software", "Kalibre");
    w.close();
    w.close();

    // The parts, and where the music ends (a whole bar at least).
    std::vector<const Track*> tracks;
    std::vector<std::vector<Note>> notes;
    Tick end = 0;
    for (auto id : ids)
        if (const auto* t = p.track (id); t && t->holdsClips())
        {
            tracks.push_back (t);
            notes.push_back (notesOf (*t));
            for (auto& n : notes.back()) end = std::max (end, snap (n.end()));
        }
    const auto& tempo = p.tempo;
    const int lastBar = std::max (0, tempo.barAt (std::max<Tick> (0, end - 1)));

    w.open ("part-list");
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        w.open ("score-part", { { "id", "P" + std::to_string (i + 1) } });
        w.leaf ("part-name", tracks[i]->name);
        w.close();
    }
    w.close();

    const int fifths = fifthsFor (p.scale);
    const bool minor = isMinor (p.scale);
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        w.open ("part", { { "id", "P" + std::to_string (i + 1) } });
        const auto voices = voicesOf (notes[i]);
        const auto clef = notation::clefFor (notes[i]);
        for (int b = 0; b <= lastBar; ++b)
        {
            const Tick barStart = tempo.barStart (b), barEnd = tempo.barStart (b + 1);
            const auto& m = tempo.meterAtBar (b);
            w.open ("measure", { { "number", std::to_string (b + 1) } });
            const bool newMeter = b == 0 || ! (tempo.meterAtBar (b - 1).numerator == m.numerator && tempo.meterAtBar (b - 1).denominator == m.denominator);
            if (b == 0 || newMeter)
            {
                w.open ("attributes");
                if (b == 0)
                {
                    w.leaf ("divisions", (long long) divisions);
                    w.open ("key");
                    w.leaf ("fifths", (long long) fifths);
                    w.leaf ("mode", minor ? "minor" : "major");
                    w.close();
                }
                w.open ("time");
                w.leaf ("beats", (long long) m.numerator);
                w.leaf ("beat-type", (long long) m.denominator);
                w.close();
                if (b == 0)
                {
                    w.open ("clef");
                    w.leaf ("sign", clef == notation::Clef::Treble ? "G" : "F");
                    w.leaf ("line", (long long) (clef == notation::Clef::Treble ? 2 : 4));
                    w.close();
                }
                w.close();
            }
            // Tempo, on the first part, where it changes in this bar.
            if (i == 0)
                for (auto& tp : tempo.tempos())
                    if (tp.tick >= barStart && tp.tick < barEnd)
                    {
                        w.open ("direction", { { "placement", "above" } });
                        w.open ("direction-type");
                        w.open ("metronome");
                        w.leaf ("beat-unit", "quarter");
                        w.leaf ("per-minute", (long long) std::lround (tp.bpm));
                        w.close();
                        w.close();
                        if (tp.tick > barStart) w.leaf ("offset", (long long) toDivisions (snap (tp.tick - barStart)));
                        w.empty ("sound", { { "tempo", std::to_string (tp.bpm) } });
                        w.close();
                    }
            const size_t used = std::max<size_t> (1, voices.size());
            for (size_t v = 0; v < used; ++v)
            {
                if (v > 0) { w.open ("backup"); w.leaf ("duration", (long long) toDivisions (barEnd - barStart)); w.close(); }
                Tick pos = barStart;
                auto gap = [&] (Tick to)
                {
                    if (to <= pos) return;
                    // Voice one fills its gaps with rests; the others move on silently.
                    if (v == 0)
                    {
                        if (pos == barStart && to == barEnd)
                        {
                            w.open ("note");
                            w.empty ("rest", { { "measure", "yes" } });
                            w.leaf ("duration", (long long) toDivisions (to - pos));
                            w.leaf ("voice", 1LL);
                            w.close();
                        }
                        else writeNotes (w, to - pos, nullptr, 0, false, false, p.scale);
                    }
                    else { w.open ("forward"); w.leaf ("duration", (long long) toDivisions (to - pos)); w.leaf ("voice", (long long) v + 1); w.close(); }
                    pos = to;
                };
                if (v < voices.size())
                    for (auto& c : voices[v])
                    {
                        if (c.end <= barStart || c.start >= barEnd) continue;
                        const Tick a = std::max (c.start, barStart), z = std::min (c.end, barEnd);
                        gap (a);
                        writeNotes (w, z - a, &c, (int) v, c.start < barStart, c.end > barEnd, p.scale);
                        pos = z;
                    }
                gap (barEnd);
            }
            w.close();
        }
        w.close();
    }
    w.close();
    return w.str();
}

//==============================================================================
Score read (const std::string& text)
{
    Score s;
    auto parsed = xml::parse (text);
    if (! parsed.root) { s.error = parsed.error.empty() ? "The file isn't readable XML." : parsed.error; return s; }
    const auto& root = *parsed.root;
    if (root.name == "score-timewise")
    {
        s.error = "This is a timewise MusicXML file, which Kalibre doesn't read. Most programs can export it partwise.";
        return s;
    }
    if (root.name != "score-partwise") { s.error = "This isn't a MusicXML score."; return s; }

    if (const auto* work = root.child ("work")) s.title = work->childText ("work-title");
    if (s.title.empty()) s.title = root.childText ("movement-title");

    struct Info { std::string name; int program = -1; std::map<std::string, int> unpitched; };
    std::map<std::string, Info> infos;
    if (const auto* list = root.child ("part-list"))
        for (const auto* sp : list->all ("score-part"))
        {
            Info info;
            info.name = sp->childText ("part-name");
            for (const auto* mi : sp->all ("midi-instrument"))
            {
                if (mi->has ("midi-program") && info.program < 0) info.program = static_cast<int> (mi->childNumber ("midi-program", -1));
                if (mi->has ("midi-unpitched")) info.unpitched[mi->attr ("id")] = static_cast<int> (mi->childNumber ("midi-unpitched", 39)) - 1;
            }
            if (info.name.empty())
                if (const auto* si = sp->child ("score-instrument")) info.name = si->childText ("instrument-name");
            infos[sp->attr ("id")] = info;
        }

    bool firstPart = true;
    for (const auto* partNode : root.all ("part"))
    {
        const auto info = infos[partNode->attr ("id")];
        Part part;
        part.name = info.name.empty() ? "Part " + std::to_string (s.parts.size() + 1) : info.name;
        part.program = info.program;
        part.percussion = ! info.unpitched.empty();

        Tick div = 1;
        int num = 4, den = 4;
        int chromatic = 0, octaveChange = 0;
        Tick measureStart = 0;
        int bar = 0;
        std::map<int, size_t> openTies;   // sounding pitch -> the note its tie carries on

        for (const auto* measure : partNode->all ("measure"))
        {
            Tick pos = 0, maxPos = 0, lastStart = 0;
            const size_t firstNote = part.notes.size();
            for (const auto& c : measure->children)
            {
                if (c.name == "attributes")
                {
                    if (c.has ("divisions")) div = std::max<Tick> (1, static_cast<Tick> (c.childNumber ("divisions", 1)));
                    if (const auto* k = c.child ("key"))
                        if (firstPart && ! s.key && k->has ("fifths"))
                            s.key = scaleFor (static_cast<int> (k->childNumber ("fifths", 0)), k->childText ("mode", "major") == "minor");
                    if (const auto* t = c.child ("time"))
                    {
                        // A compound signature like 3+2/8 adds its beats up.
                        int beats = 0;
                        const auto bt = t->childText ("beats", "4");
                        for (size_t a = 0; a < bt.size();)
                        {
                            size_t z = a;
                            while (z < bt.size() && std::isdigit (static_cast<unsigned char> (bt[z]))) ++z;
                            if (z > a) beats += std::stoi (bt.substr (a, z - a));
                            a = z + 1;
                        }
                        num = std::max (1, beats);
                        den = std::max (1, static_cast<int> (t->childNumber ("beat-type", 4)));
                        if (firstPart && (s.meters.empty() || ! (s.meters.back().numerator == num && s.meters.back().denominator == den)))
                            s.meters.push_back ({ bar, num, den });
                    }
                    for (const auto* cl : c.all ("clef"))
                        if (cl->childText ("sign") == "percussion") part.percussion = true;
                    if (const auto* tr = c.child ("transpose"))
                    {
                        chromatic = static_cast<int> (tr->childNumber ("chromatic", 0));
                        octaveChange = static_cast<int> (tr->childNumber ("octave-change", 0));
                    }
                }
                else if (c.name == "direction" || c.name == "sound")
                {
                    const auto* sound = c.name == "sound" ? &c : c.child ("sound");
                    if (firstPart && sound != nullptr && ! sound->attr ("tempo").empty())
                    {
                        const Tick offset = c.has ("offset") ? static_cast<Tick> (c.childNumber ("offset", 0)) * PPQ / div : 0;
                        s.tempos.push_back ({ measureStart + pos + offset, std::atof (sound->attr ("tempo").c_str()), false });
                    }
                }
                else if (c.name == "backup") pos -= static_cast<Tick> (c.childNumber ("duration", 0)) * PPQ / div;
                else if (c.name == "forward") { pos += static_cast<Tick> (c.childNumber ("duration", 0)) * PPQ / div; maxPos = std::max (maxPos, pos); }
                else if (c.name == "note")
                {
                    if (c.has ("grace") || c.has ("cue")) continue;
                    const Tick dur = static_cast<Tick> (std::llround (c.childNumber ("duration", 0) * static_cast<double> (PPQ) / static_cast<double> (div)));
                    const bool chord = c.has ("chord");
                    const Tick start = measureStart + (chord ? lastStart : pos);
                    if (! chord) { lastStart = pos; pos += dur; maxPos = std::max (maxPos, pos); }
                    if (c.has ("rest") || dur <= 0) continue;

                    int pitch = -1;
                    if (const auto* pn = c.child ("pitch"))
                    {
                        const int alter = static_cast<int> (std::lround (pn->childNumber ("alter", 0)));
                        const int octave = static_cast<int> (pn->childNumber ("octave", 4));
                        pitch = (octave + 1) * 12 + stepPitches[letterOf (pn->childText ("step", "C"))] + alter + chromatic + 12 * octaveChange;
                    }
                    else if (const auto* u = c.child ("unpitched"))
                    {
                        part.percussion = true;
                        pitch = 38;
                        if (const auto* ins = c.child ("instrument"))
                        {
                            if (auto it = info.unpitched.find (ins->attr ("id")); it != info.unpitched.end()) pitch = it->second;
                        }
                        else
                        {
                            // No instrument: the line or space says which drum (kick low, snare middle, hi-hat high).
                            const int step = static_cast<int> (u->childNumber ("display-octave", 4)) * 7 + letterOf (u->childText ("display-step", "C")) - (4 * 7 + 2);
                            pitch = step <= 1 ? 36 : step >= 9 ? 42 : 38;
                        }
                    }
                    if (pitch < 0 || pitch > 127) continue;

                    bool tieStop = false, tieStart = false;
                    for (const auto* t : c.all ("tie"))
                    {
                        if (t->attr ("type") == "stop") tieStop = true;
                        if (t->attr ("type") == "start") tieStart = true;
                    }
                    // A tie carries the note on: it's lengthened, not started again.
                    if (tieStop)
                        if (auto it = openTies.find (pitch); it != openTies.end() && it->second < part.notes.size())
                        {
                            auto& prev = part.notes[it->second];
                            if (std::abs (prev.end() - start) <= PPQ / 32)
                            {
                                prev.length = start + dur - prev.start;
                                if (! tieStart) openTies.erase (it);
                                continue;
                            }
                        }
                    Note n;
                    n.start = start;
                    n.length = std::max<Tick> (1, dur);
                    n.pitch = pitch;
                    const auto dyn = c.attr ("dynamics");
                    n.velocity = dyn.empty() ? 100 : std::clamp (static_cast<int> (std::lround (std::atof (dyn.c_str()) * 0.9)), 1, 127);
                    part.notes.push_back (n);
                    if (tieStart) openTies[pitch] = part.notes.size() - 1;
                }
            }
            const Tick barLength = static_cast<Tick> (num) * PPQ * 4 / den;
            // A pickup bar is the end of a bar: its notes move to the end of the first bar.
            if (bar == 0 && measure->attr ("implicit") == "yes" && maxPos > 0 && maxPos < barLength)
                for (size_t k = firstNote; k < part.notes.size(); ++k) part.notes[k].start += barLength - maxPos;
            measureStart += barLength;
            ++bar;
        }
        s.length = std::max (s.length, measureStart);
        s.parts.push_back (std::move (part));
        firstPart = false;
    }
    if (s.parts.empty()) { s.error = "This MusicXML file has no parts in it."; return s; }
    s.ok = true;
    return s;
}

} // namespace kal::musicxml
