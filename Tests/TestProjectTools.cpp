#include "Check.h"
#include "Edit.h"
#include "ProjectTools.h"

using namespace kal;

static Project withAudio()
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Vox");
    Clip c;
    c.kind = Clip::Kind::Audio;
    c.name = "Take";
    c.start = PPQ * 4;           // 2 s at 120
    c.length = PPQ * 4;          // 2 s
    c.audio.file = "/old/place/Vocal.wav";
    c.audio.sourceStart = 1.0;
    edit::addClip (p, t, c);
    c.audio.file = "/old/place/Gtr.wav";
    c.start = PPQ * 12;
    edit::addClip (p, t, c);
    p.track (t)->frozenFile = "/old/place/Vocal.wav";   // the same file twice is listed once
    return p;
}

TEST ("project tools: files are listed once each, and moved together")
{
    auto p = withAudio();
    const auto files = projecttools::referencedFiles (p);
    CHECK_EQ (files.size(), (size_t) 2);
    projecttools::replaceFiles (p, { { "/old/place/Vocal.wav", "Audio/Vocal.wav" } });
    CHECK_EQ (p.tracks[0].clips()[0].audio.file, std::string ("Audio/Vocal.wav"));
    CHECK_EQ (p.tracks[0].frozenFile, std::string ("Audio/Vocal.wav"));
    CHECK_EQ (p.tracks[0].clips()[1].audio.file, std::string ("/old/place/Gtr.wav"));
}

TEST ("project tools: missing files are found again by name")
{
    const auto found = projecttools::relinkByName ({ "/gone/Vocal.wav", "/gone/Bass.wav" },
                                                   { "/new/drive/vocal.WAV", "/new/drive/Keys.wav" });
    CHECK_EQ (found.size(), (size_t) 1);
    CHECK_EQ (found.at ("/gone/Vocal.wav"), std::string ("/new/drive/vocal.WAV"));
}

TEST ("project tools: a template keeps the tracks and plug-ins, not the music")
{
    auto p = withAudio();
    PluginRef r;
    r.name = "Comp";
    edit::addDevice (p, p.tracks[0].id, r, DeviceRole::AudioFx);
    p.markers.push_back ({ 1, 0, "Intro" });
    const auto t = projecttools::asTemplate (p, false);
    CHECK_EQ (t.tracks.size(), (size_t) 1);
    CHECK_EQ (t.tracks[0].devices.size(), (size_t) 1);
    CHECK (t.tracks[0].clips().empty());
    CHECK (t.markers.empty());
    CHECK (projecttools::referencedFiles (t).empty());
}

TEST ("project tools: an ID3 tag carries the song's details")
{
    Metadata m;
    m.title = "Song";
    m.artist = "Me";
    m.year = 2026;
    const auto tag = projecttools::id3v2 (m);
    CHECK (tag.size() > 10);
    CHECK_EQ (tag[0], (unsigned char) 'I');
    CHECK_EQ (tag[3], (unsigned char) 3);
    const std::string body (tag.begin() + 10, tag.end());
    CHECK (body.find ("TIT2") != std::string::npos && body.find ("Song") != std::string::npos);
    const size_t size = (size_t) (tag[6] << 21 | tag[7] << 14 | tag[8] << 7 | tag[9]);
    CHECK_EQ (size, tag.size() - 10);
    CHECK (projecttools::id3v2 ({}).empty());
}

TEST ("project tools: Final Cut XML places each audio clip in frames")
{
    auto p = withAudio();
    const auto xml = projecttools::fcpXml (p, [] (const std::string&) { return 10.0; }, 25);
    CHECK (xml.find ("<xmeml version=\"5\">") != std::string::npos);
    CHECK (xml.find ("<start>50</start>") != std::string::npos);   // 2 s
    CHECK (xml.find ("<end>100</end>") != std::string::npos);
    CHECK (xml.find ("<in>25</in>") != std::string::npos);         // 1 s into the file
    CHECK (xml.find ("file:///old/place/Vocal.wav") != std::string::npos);
    // A Windows path as a file URL (0129).
    projecttools::replaceFiles (p, { { "/old/place/Vocal.wav", "C:\\Media\\My Vocal.wav" } });
    const auto win = projecttools::fcpXml (p, [] (const std::string&) { return 10.0; }, 25);
    CHECK (win.find ("file:///C:/Media/My%20Vocal.wav") != std::string::npos);
}
