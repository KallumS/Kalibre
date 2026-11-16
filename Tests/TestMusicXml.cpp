// MusicXML in and out (decision 0057).
#include "Check.h"
#include "Edit.h"
#include "MusicXml.h"
#include "Xml.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <tuple>

using namespace kal;

namespace
{
std::multiset<std::tuple<Tick, Tick, int>> notesOf (const std::vector<Note>& notes)
{
    std::multiset<std::tuple<Tick, Tick, int>> out;
    for (auto& n : notes) out.insert ({ n.start, n.length, n.pitch });
    return out;
}

void note (Project& p, Id clip, double beat, double beats, int pitch, int velocity = 100)
{
    Note n;
    n.start = (Tick) std::llround (beat * PPQ);
    n.length = (Tick) std::llround (beats * PPQ);
    n.pitch = pitch;
    n.velocity = velocity;
    edit::addNote (p, clip, n);
}
} // namespace

TEST ("musicxml: tracks go out and come back: chords, ties over the bar line, voices, time signatures, tempo and key")
{
    Project p;
    p.name = "Round & Trip";
    p.tempo.setTempos ({ { 0, 96.0, false } });
    p.tempo.setMeters ({ { 0, 4, 4 }, { 2, 3, 4 } });
    p.scale = musicxml::scaleFor (-6, true);   // Eb minor
    const Id lead = edit::addTrack (p, TrackKind::Track, "Lead");
    const Id c1 = edit::addMidiClip (p, lead, 0, PPQ * 11);
    note (p, c1, 0, 0.5, 63); note (p, c1, 0.5, 0.5, 65); note (p, c1, 1, 0.75, 66); note (p, c1, 1.75, 0.25, 68);
    note (p, c1, 2, 1, 70, 64);
    note (p, c1, 3, 3, 66);                          // across into bar 2
    note (p, c1, 8, 3, 63);                          // bar 3, in 3/4
    const Id keys = edit::addTrack (p, TrackKind::Track, "Keys");
    const Id c2 = edit::addMidiClip (p, keys, PPQ * 4, PPQ * 4);   // a clip that starts in bar 2
    for (int pitch : { 51, 54, 58 }) note (p, c2, 0, 2, pitch);    // a chord
    note (p, c2, 1, 2, 39);                          // overlapping it: a second voice

    const auto text = musicxml::write (p, { lead, keys });
    CHECK (text.find ("<score-partwise") != std::string::npos);
    CHECK (text.find ("<fifths>-6</fifths>") != std::string::npos);
    CHECK (text.find ("<mode>minor</mode>") != std::string::npos);
    CHECK (text.find ("Round &amp; Trip") != std::string::npos);
    CHECK (text.find ("<alter>-1</alter>") != std::string::npos);   // spelled with flats, as Eb minor is

    // Every voice of every bar adds up to the bar's length: what notation programs check first.
    {
        auto doc = xml::parse (text);
        CHECK (doc.root != nullptr);
        int div = 8, beats = 4, type = 4, wrong = 0;
        for (const auto* part : doc.root->all ("part"))
            for (const auto* m : part->all ("measure"))
            {
                if (const auto* a = m->child ("attributes"))
                {
                    div = (int) a->childNumber ("divisions", div);
                    if (const auto* t = a->child ("time")) { beats = (int) t->childNumber ("beats", 4); type = (int) t->childNumber ("beat-type", 4); }
                }
                const int length = beats * div * 4 / type;
                int pos = 0;
                std::map<std::string, int> filled;
                for (const auto& e : m->children)
                {
                    if (e.name == "note" && ! e.has ("chord")) { pos += (int) e.childNumber ("duration", 0); filled[e.childText ("voice")] = std::max (filled[e.childText ("voice")], pos); }
                    if (e.name == "forward") { pos += (int) e.childNumber ("duration", 0); filled[e.childText ("voice")] = std::max (filled[e.childText ("voice")], pos); }
                    if (e.name == "backup") pos -= (int) e.childNumber ("duration", 0);
                }
                for (auto& [v, f] : filled) if (f != length) ++wrong;
            }
        CHECK_EQ (wrong, 0);
        CHECK (text.find ("<voice>2</voice>") != std::string::npos);   // the overlapping bass got its own voice
        CHECK (text.find ("<chord/>") != std::string::npos);
    }

    const auto back = musicxml::read (text);
    CHECK (back.ok);
    if (! back.ok) { std::printf ("%s\n", back.error.c_str()); return; }
    CHECK_EQ (back.title, std::string ("Round & Trip"));
    CHECK_EQ (back.parts.size(), size_t (2));
    CHECK_EQ (back.parts[0].name, std::string ("Lead"));
    std::vector<Note> lead0, keys0;
    for (auto& n : p.clip (c1)->midi.notes) lead0.push_back (n);
    for (auto n : p.clip (c2)->midi.notes) { n.start += PPQ * 4; keys0.push_back (n); }
    CHECK (notesOf (back.parts[0].notes) == notesOf (lead0));
    CHECK (notesOf (back.parts[1].notes) == notesOf (keys0));
    // Loudness carried.
    for (auto& n : back.parts[0].notes) if (n.pitch == 70) CHECK (std::abs (n.velocity - 64) <= 1);
    CHECK_EQ (back.meters.size(), size_t (2));
    if (back.meters.size() == 2) { CHECK_EQ (back.meters[1].bar, 2); CHECK_EQ (back.meters[1].numerator, 3); }
    CHECK_EQ (back.tempos.size(), size_t (1));
    if (! back.tempos.empty()) CHECK (std::abs (back.tempos[0].bpm - 96.0) < 0.01);
    CHECK (back.key.has_value());
    if (back.key) { CHECK_EQ (back.key->root, p.scale.root); CHECK_EQ (back.key->scale, p.scale.scale); }
}

TEST ("musicxml: a file as notation programs write it: divisions, a pickup, a transposing part, a tie, drums")
{
    const std::string text = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 3.1 Partwise//EN" "http://www.musicxml.org/dtds/partwise.dtd">
<score-partwise version="3.1">
  <movement-title>Little Tune</movement-title>
  <part-list>
    <score-part id="P1"><part-name>Clarinet in B&#x266d;</part-name><midi-instrument id="P1-I1"><midi-program>72</midi-program></midi-instrument></score-part>
    <score-part id="P2"><part-name>Drums</part-name>
      <score-instrument id="P2-I36"><instrument-name>Kick</instrument-name></score-instrument>
      <midi-instrument id="P2-I36"><midi-unpitched>37</midi-unpitched></midi-instrument></score-part>
  </part-list>
  <part id="P1">
    <measure number="0" implicit="yes">
      <attributes><divisions>2</divisions><key><fifths>1</fifths><mode>major</mode></key><time><beats>4</beats><beat-type>4</beat-type></time>
        <transpose><diatonic>-1</diatonic><chromatic>-2</chromatic></transpose></attributes>
      <direction><sound tempo="132"/></direction>
      <note><pitch><step>D</step><octave>5</octave></pitch><duration>2</duration><voice>1</voice><type>quarter</type></note>
    </measure>
    <measure number="1">
      <note><grace/><pitch><step>F</step><octave>5</octave></pitch><voice>1</voice><type>eighth</type></note>
      <note><pitch><step>E</step><octave>5</octave></pitch><duration>6</duration><tie type="start"/><voice>1</voice><type>half</type><dot/></note>
      <note><pitch><step>E</step><octave>5</octave></pitch><duration>2</duration><tie type="stop"/><voice>1</voice><type>quarter</type></note>
    </measure>
  </part>
  <part id="P2">
    <measure number="0" implicit="yes"><attributes><divisions>1</divisions><clef><sign>percussion</sign></clef></attributes>
      <note><unpitched><display-step>F</display-step><display-octave>4</display-octave></unpitched><duration>1</duration><instrument id="P2-I36"/><voice>1</voice></note>
    </measure>
  </part>
</score-partwise>)";
    const auto s = musicxml::read (text);
    CHECK (s.ok);
    if (! s.ok) return;
    CHECK_EQ (s.title, std::string ("Little Tune"));
    CHECK_EQ (s.parts.size(), size_t (2));
    const auto& cl = s.parts[0];
    CHECK_EQ (cl.program, 72);
    CHECK_EQ (cl.notes.size(), size_t (2));   // the grace note skipped; the tie made one note
    if (cl.notes.size() == 2)
    {
        // The pickup moves to the end of bar one; written D5 sounds C5 on a Bb clarinet.
        CHECK_EQ (cl.notes[0].start, Tick (PPQ * 3));
        CHECK_EQ (cl.notes[0].pitch, 72);
        CHECK_EQ (cl.notes[1].start, Tick (PPQ * 4));
        CHECK_EQ (cl.notes[1].length, Tick (PPQ * 4));
        CHECK_EQ (cl.notes[1].pitch, 74);
    }
    CHECK (s.parts[1].percussion);
    if (! s.parts[1].notes.empty()) CHECK_EQ (s.parts[1].notes[0].pitch, 36);   // its MIDI instrument: a kick
    CHECK (s.tempos.size() == 1 && std::abs (s.tempos[0].bpm - 132.0) < 0.01);
    CHECK (s.key.has_value() && s.key->scale == 0 && musicxml::fifthsFor (*s.key) == 1);   // G major
    CHECK_EQ (s.length, Tick (PPQ * 8));
}

TEST ("musicxml: key signatures and scales go both ways")
{
    for (int f = -7; f <= 7; ++f)
        for (bool minor : { false, true })
        {
            const auto sc = musicxml::scaleFor (f, minor);
            CHECK_EQ (musicxml::fifthsFor (sc), f);
            CHECK_EQ (musicxml::isMinor (sc), minor);
        }
}

TEST ("musicxml: what isn't a partwise score is refused, with a reason")
{
    CHECK (! musicxml::read ("<score-timewise/>").ok);
    CHECK (musicxml::read ("<score-timewise/>").error.find ("timewise") != std::string::npos);
    CHECK (! musicxml::read ("<html><body/></html>").ok);
    CHECK (! musicxml::read ("not xml").ok);
}
