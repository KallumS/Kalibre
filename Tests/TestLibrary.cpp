// The browser's Sounds tab (decision 0094): the folders, favourites and order.
#include "Check.h"
#include "Library.h"

#include <algorithm>
#include <set>

using namespace kal;
using namespace kal::library;

namespace
{
Item audio (const std::string& path)
{
    Item it;
    it.path = path;
    it.seconds = 1;
    it.sampleRate = 48000;
    return it;
}

Index sampleIndex()
{
    Index ix;
    for (auto p : { "/M/Drums/Kicks/Kick 1.wav", "/M/Drums/Kicks/Kick 2.wav", "/M/Drums/Snare.wav",
                    "/M/Keys/Chords.mid", "/M/Keys/Pad 90bpm.flac", "/Elsewhere/Not Chosen.wav" })
        ix.items[p] = audio (p);
    ix.items["/M/Keys/Chords.mid"].kind = Kind::Midi;
    Item broken = audio ("/M/Drums/Broken.wav");
    broken.readable = false;
    ix.items[broken.path] = broken;
    return ix;
}

std::vector<std::string> names (const std::vector<Row>& rs)
{
    std::vector<std::string> out;
    for (auto& r : rs) out.push_back (std::string (r.depth * 2, ' ') + (r.type == Row::Type::Folder ? "[" + nameOf (r.path) + "]" : nameOf (r.path)));
    return out;
}
} // namespace

TEST ("library: a tempo is read from a file's name, wherever it's written")
{
    CHECK_EQ (tempoFromName ("Drums 96bpm"), 96.0);
    CHECK_EQ (tempoFromName ("loop_120_BPM"), 120.0);
    CHECK_EQ (tempoFromName ("Pad 87.5 bpm"), 87.5);
    CHECK_EQ (tempoFromName ("BPM140 Hats"), 140.0);
    CHECK_EQ (tempoFromName ("Kick 01"), 0.0);
    CHECK_EQ (tempoFromName ("bpm"), 0.0);
    CHECK_EQ (tempoFromName ("Weird 5bpm"), 0.0);       // outside 40-300
}

TEST ("library: paths, extensions and what's inside what")
{
    CHECK_EQ (nameOf ("/M/Drums/Kick.WAV"), std::string ("Kick.WAV"));
    CHECK_EQ (extensionOf ("/M/Drums/Kick.WAV"), std::string (".wav"));
    CHECK_EQ (extensionOf ("/M/.hidden"), std::string());
    CHECK_EQ (parentOf ("/M/Drums/Kick.wav"), std::string ("/M/Drums"));
    CHECK (isInside ("/M/Drums/Kick.wav", "/M"));
    CHECK (isInside ("/M/Drums/Kick.wav", "/M/"));
    CHECK (! isInside ("/Music/Kick.wav", "/M"));
    CHECK (isMidiExtension (".MID"));
    CHECK (! isMidiExtension (".wav"));
}

TEST ("library: in folders, a chosen folder opens to its folders then its items; empty and unreadable ones aren't shown")
{
    const auto ix = sampleIndex();
    Settings s;
    s.folders = { "/M/" };
    // Closed: just the folder, with how many items it holds (the unreadable one not counted).
    auto rs = rows (ix, s);
    CHECK_EQ (rs.size(), (size_t) 1);
    CHECK_EQ (rs[0].count, 5);
    s.open = { "/M", "/M/Drums", "/M/Drums/Kicks", "/M/Keys" };
    const auto shown = names (rows (ix, s));
    const std::vector<std::string> expected { "[M]", "  [Drums]", "    [Kicks]", "      Kick 1.wav", "      Kick 2.wav", "    Snare.wav",
                                              "  [Keys]", "    Chords.mid", "    Pad 90bpm.flac" };
    CHECK (shown == expected);
}

TEST ("library: every item in one list, A to Z, from every chosen folder")
{
    const auto ix = sampleIndex();
    Settings s;
    s.folders = { "/M" };
    s.flat = true;
    const auto shown = names (rows (ix, s));
    const std::vector<std::string> expected { "Chords.mid", "Kick 1.wav", "Kick 2.wav", "Pad 90bpm.flac", "Snare.wav" };
    CHECK (shown == expected);
}

TEST ("library: shuffled, the order is the same for the same shuffle, new for a new one, and items stay in their folders")
{
    Index ix;
    for (int i = 0; i < 24; ++i)
    {
        const auto p = std::string ("/S/") + (i < 12 ? "A/" : "B/") + "Sample " + std::to_string (i) + ".wav";
        ix.items[p] = audio (p);
    }
    Settings s;
    s.folders = { "/S" };
    s.flat = true;
    s.shuffled = true;
    s.seed = 7;
    const auto once = itemsInOrder (rows (ix, s));
    CHECK (once == itemsInOrder (rows (ix, s)));
    s.seed = 8;
    const auto again = itemsInOrder (rows (ix, s));
    CHECK (once != again);
    auto sorted = again;
    std::sort (sorted.begin(), sorted.end());
    auto all = once;
    std::sort (all.begin(), all.end());
    CHECK (sorted == all);   // the same items, another order
    // In folders: shuffled inside each, each item under its own folder.
    s.flat = false;
    s.open = { "/S", "/S/A", "/S/B" };
    const auto rs = rows (ix, s);
    std::string folder;
    int checked = 0;
    for (auto& r : rs)
    {
        if (r.type == Row::Type::Folder) { folder = r.path; continue; }
        CHECK_EQ (parentOf (r.path), folder);
        ++checked;
    }
    CHECK_EQ (checked, 24);
    std::vector<std::string> inA;
    for (auto& p : itemsInOrder (rs)) if (parentOf (p) == "/S/A") inA.push_back (p);
    CHECK (! std::is_sorted (inA.begin(), inA.end(), [] (const std::string& a, const std::string& b) { return nameOf (a) < nameOf (b); }));
}

TEST ("library: favourites, of items and of folders; Favourites Only shows them, folders opened")
{
    const auto ix = sampleIndex();
    Settings s;
    s.folders = { "/M" };
    s.favourites = { "/M/Drums/Kicks", "/M/Keys/Chords.mid" };
    CHECK (isFavourite (s, "/M/Drums/Kicks/Kick 1.wav"));   // inside a favourite folder
    CHECK (isFavourite (s, "/M/Keys/Chords.mid"));
    CHECK (! isFavourite (s, "/M/Drums/Snare.wav"));
    s.favouritesOnly = true;
    const auto shown = names (rows (ix, s));
    const std::vector<std::string> expected { "[M]", "  [Drums]", "    [Kicks]", "      Kick 1.wav", "      Kick 2.wav", "  [Keys]", "    Chords.mid" };
    CHECK (shown == expected);
    s.flat = true;
    CHECK_EQ (rows (ix, s).size(), (size_t) 3);
}

TEST ("library: a search keeps the items whose names hold it, and opens their folders")
{
    const auto ix = sampleIndex();
    Settings s;
    s.folders = { "/M" };
    const auto shown = names (rows (ix, s, "KICK"));
    const std::vector<std::string> expected { "[M]", "  [Drums]", "    [Kicks]", "      Kick 1.wav", "      Kick 2.wav" };
    CHECK (shown == expected);
    CHECK (rows (ix, s, "nothing like it").empty());
}

TEST ("library: a file is read again only when its size or date changes")
{
    Item it = audio ("/M/a.wav");
    it.size = 100;
    it.modified = 5;
    it.analysis = library::analysisVersion;
    CHECK (needsScan (nullptr, 100, 5));
    CHECK (! needsScan (&it, 100, 5));
    CHECK (needsScan (&it, 101, 5));
    CHECK (needsScan (&it, 100, 6));
}

TEST ("library: the waveform's columns are the loudest of what falls in each")
{
    std::vector<float> loud (320, 0.0f);
    loud[0] = 1.0f;
    loud[319] = -0.5f;
    const auto p = peaksFrom (loud);
    CHECK_EQ ((int) p.size(), peakColumns);
    CHECK_EQ ((int) p.front(), 255);
    CHECK_EQ ((int) p.back(), 128);
    CHECK_EQ ((int) p[80], 0);
    CHECK_EQ ((int) peaksFrom ({ 0.5f })[0], 128);   // fewer values than columns: stretched
}

TEST ("library: the settings and the index are saved and read back as they were")
{
    Settings s;
    s.folders = { "/M", "/Volumes/Samples" };
    s.asked = true;
    s.favourites = { "/M/Drums" };
    s.flat = true;
    s.shuffled = true;
    s.seed = 99;
    s.sync = false;
    s.open = { "/M" };
    Settings back;
    CHECK (fromJson (toJson (s), back));
    CHECK (back == s);
    CHECK (! fromJson ("not json", back));

    Index ix;
    Item a = audio ("/M/a.wav");
    a.size = 10; a.modified = 20; a.bpm = 120; a.channels = 2;
    a.peaks = peaksFrom ({ 0.1f, 0.9f, 0.4f });
    Item m;
    m.path = "/M/b.mid";
    m.kind = Kind::Midi;
    m.beats = 16;
    m.notes = { { 0, 1, 60 }, { 1.5f, 0.5f, 64 } };
    ix.items[a.path] = a;
    ix.items[m.path] = m;
    Index ib;
    CHECK (fromJson (toJson (ix), ib));
    CHECK (ib.items == ix.items);
}

TEST ("library: the scan's loudness, note and tempo are kept; an older scan's item is read again (0095)")
{
    library::Index index;
    library::Item it;
    it.path = "/s/Pad.wav";
    it.size = 10;
    it.modified = 20;
    it.lufs = -14.25;
    it.peakDb = -0.5;
    it.note = 57;
    it.cents = -12;
    it.bpm = 120;
    it.bpmFrom = library::Item::TempoFrom::Heard;
    it.analysis = library::analysisVersion;
    index.items[it.path] = it;
    library::Index back;
    CHECK (library::fromJson (library::toJson (index), back));
    CHECK (back.items[it.path] == it);
    CHECK (! library::needsScan (&back.items[it.path], 10, 20));
    back.items[it.path].analysis = 1;
    CHECK (library::needsScan (&back.items[it.path], 10, 20));
    // Middle C is C4, as the piano roll, the Sampler and the drum pads name it (the UAT found C3 here).
    CHECK_EQ (library::noteName (57, -12), std::string ("A3 -12"));
    CHECK_EQ (library::noteName (60, 0.2), std::string ("C4"));
    CHECK_EQ (library::noteName (21, 0), std::string ("A0"));
    CHECK_EQ (library::noteName (-1, 0), std::string());
    CHECK_EQ (library::loudnessText (it), std::string ("-14.2 LUFS"));
    it.lufs = library::silentDb;
    CHECK_EQ (library::loudnessText (it), std::string ("Peak -0.5 dB"));
}

#include <sstream>

TEST ("library: the index saved a sound a line comes back the same; one saved by an older Kalibre, as one document, still opens (0106)")
{
    library::Index ix;
    for (int i = 0; i < 3; ++i)
    {
        library::Item it;
        it.path = "/Sounds/Kick " + std::to_string (i) + ".wav";
        it.seconds = 0.5 + i;
        it.size = 1000 + i;
        it.modified = 1700000000000 + i;
        it.peaks = { 10, 200, 30 };
        it.onsetMs = 1;
        it.attackMs = 2;
        it.tailDb = -40;
        it.zcr = 300;
        it.analysis = library::analysisVersion;
        ix.items[it.path] = it;
    }
    std::stringstream lines;
    CHECK (library::writeIndex (ix, lines));
    // A header and one line a sound.
    int count = 0;
    for (std::string l; std::getline (lines, l);) ++count;
    CHECK_EQ (count, 4);
    lines.clear();
    lines.seekg (0);
    library::Index back;
    CHECK (library::readIndex (lines, back));
    CHECK (back.items == ix.items);
    std::stringstream old (library::toJson (ix));
    library::Index fromOld;
    CHECK (library::readIndex (old, fromOld));
    CHECK (fromOld.items == ix.items);
}

TEST ("library: Random Sample chooses any sound the scan read, of any kind, never MIDI, the broken, the empty, the too long or the one it has; a new one each seed (0108)")
{
    Index ix = sampleIndex();
    Item silent = audio ("/M/Empty.wav");
    silent.seconds = 0;
    ix.items[silent.path] = silent;
    Item tooLong = audio ("/M/Field Recording.wav");
    tooLong.seconds = 900;
    ix.items[tooLong.path] = tooLong;
    Item loop = audio ("/M/Loops/Break 120bpm.wav");
    loop.seconds = 8;
    ix.items[loop.path] = loop;
    std::set<std::string> seen;
    for (unsigned seed = 1; seed <= 400; ++seed)
    {
        const auto p = randomSound (ix, seed);
        CHECK (! p.empty());
        CHECK (p != "/M/Keys/Chords.mid");
        CHECK (p != "/M/Drums/Broken.wav");
        CHECK (p != "/M/Empty.wav");
        CHECK (p != "/M/Field Recording.wav");
        seen.insert (p);
        CHECK (randomSound (ix, seed) == p);   // the same seed, the same choice
    }
    // Every one that fits comes up: drums, a pad, a loop, wherever it is.
    CHECK_EQ (seen.size(), (size_t) 6);
    CHECK (seen.count ("/M/Loops/Break 120bpm.wav") == 1);
    CHECK (seen.count ("/M/Keys/Pad 90bpm.flac") == 1);
    // Never the one it has, unless there's no other.
    for (unsigned seed = 1; seed <= 50; ++seed) CHECK (randomSound (ix, seed, "/M/Drums/Snare.wav") != "/M/Drums/Snare.wav");
    Index one;
    one.items["/A.wav"] = audio ("/A.wav");
    CHECK (randomSound (one, 1, "/A.wav").empty());
    CHECK (randomSound (Index {}, 1).empty());
}
