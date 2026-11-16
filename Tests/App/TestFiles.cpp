#include "Check.h"
#include "Controller.h"
#include "Edit.h"
#include "ProjectTools.h"

using namespace kal;
using namespace juce;

namespace
{
File scratch()
{
    auto f = File::getSpecialLocation (File::tempDirectory).getChildFile ("KalibreFileTests");
    f.deleteRecursively();
    f.createDirectory();
    return f;
}

File tone (const File& f, double seconds = 1.0)
{
    AudioBuffer<float> b (2, (int) (48000 * seconds));
    for (int i = 0; i < b.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch) b.setSample (ch, i, 0.3f * std::sin ((float) i * 0.05f));
    WavAudioFormat wav;
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    std::unique_ptr<OutputStream> os (f.createOutputStream().release());
    auto w = wav.createWriterFor (os, AudioFormatWriterOptions().withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    return f;
}

Clip clipOf (const File& f, Tick start)
{
    Clip c;
    c.kind = Clip::Kind::Audio;
    c.name = f.getFileNameWithoutExtension().toStdString();
    c.start = start;
    c.length = PPQ * 2;
    c.audio.file = f.getFullPathName().toStdString();
    c.audio.sourceLength = 1.0;
    return c;
}
} // namespace

TEST ("files: collect copies outside files into the project's folder; relink finds moved ones; archive zips it all")
{
    const auto dir = scratch();
    const auto outside = dir.getChildFile ("Elsewhere");
    outside.createDirectory();
    const auto vocal = tone (outside.getChildFile ("Vocal.wav"));
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Vox");
    edit::addClip (c.project, t, clipOf (vocal, 0));
    const auto song = dir.getChildFile ("Song").getChildFile ("Song.kalibre");
    song.getParentDirectory().createDirectory();
    CHECK (c.saveAs (song));
    CHECK (c.collectAndSave());
    CHECK_EQ (String (c.project.track (t)->clips()[0].audio.file), String ("Audio/Vocal.wav"));
    CHECK (song.getParentDirectory().getChildFile ("Audio/Vocal.wav").existsAsFile());
    CHECK (c.missingFiles().empty());

    // The archive holds the project and its audio.
    const auto zip = dir.getChildFile ("Song.zip");
    CHECK (c.archive (zip));
    ZipFile z (zip);
    CHECK (z.getIndexOfFileName ("Song/Song.kalibre") >= 0);
    CHECK (z.getIndexOfFileName ("Song/Audio/Vocal.wav") >= 0);

    // A file that moved is found by name in a folder.
    const auto moved = tone (dir.getChildFile ("Found").getChildFile ("Gtr.wav"));
    edit::addClip (c.project, t, clipOf (File ("/nowhere/at/all/Gtr.wav"), PPQ * 4));
    CHECK_EQ (c.missingFiles().size(), (size_t) 1);
    CHECK_EQ (c.relinkFrom (dir.getChildFile ("Found")), 1);
    CHECK (c.missingFiles().empty());
    ignoreUnused (moved);
}

TEST ("files: Final Cut XML goes out and comes back with the clips where they were")
{
    const auto dir = scratch();
    const auto a = tone (dir.getChildFile ("A.wav"));
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Vox");
    edit::addClip (c.project, t, clipOf (a, PPQ * 4));   // 2 s in, at 120
    const auto xml = dir.getChildFile ("Song.xml");
    CHECK (c.exportXml (xml));
    Controller d (engine);
    const auto before = d.project.tracks.size();
    CHECK (d.importXml (xml));
    CHECK_EQ (d.project.tracks.size(), before + 1);
    if (! d.project.tracks.empty() && ! d.project.tracks.back().clips().empty())
    {
        const auto& clip = d.project.tracks.back().clips()[0];
        CHECK_EQ (clip.start, PPQ * 4);
        CHECK_EQ (clip.length, PPQ * 2);
        CHECK_EQ (String (clip.audio.file), a.getFullPathName());
    }
}

TEST ("files: an MP3 export with its tags; a template starts a song with the same tracks and no clips")
{
    const auto dir = scratch();
    const auto a = tone (dir.getChildFile ("A.wav"), 2.0);
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    const Id t = edit::addTrack (c.project, TrackKind::Track, "Vox");
    edit::addClip (c.project, t, clipOf (a, 0));
    c.project.metadata.title = "Song Title";
    c.project.metadata.artist = "Someone";
    Engine::RenderOptions o;
    o.start = 0; o.end = 1.0; o.tail = 0; o.format = "mp3";
    const auto mp3 = dir.getChildFile ("Song.mp3");
    CHECK (c.exportAudio (mp3, o, false));
    CHECK (mp3.getSize() > 10000);   // a second at 320 kb/s is 40 kB
    MemoryBlock head;
    mp3.loadFileAsData (head);
    CHECK (head.getSize() > 3 && head[0] == 'I' && head[1] == 'D' && head[2] == '3');
    const std::string bytes ((const char*) head.getData(), jmin ((size_t) 400, head.getSize()));
    CHECK (bytes.find ("Song Title") != std::string::npos);

    CHECK (c.saveTemplate ("Test Template", false));
    const auto tf = c.templatesFolder().getChildFile ("Test Template.kalibre");
    Controller d (engine);
    CHECK (d.newFromTemplate (tf));
    CHECK_EQ (d.project.tracks.size(), c.project.tracks.size());
    for (auto& tr : d.project.tracks) CHECK (tr.clips().empty());
    tf.deleteFile();
}

TEST ("files: a video goes on the timeline at the cursor, its sound on a track of its own")
{
    const auto dir = scratch();
    const auto film = tone (dir.getChildFile ("Film.wav"));   // stands in for a film with sound
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    const auto before = c.project.tracks.size();
    CHECK (c.importVideo (film, PPQ * 4));
    CHECK_EQ (String (c.project.video.file), film.getFullPathName());
    CHECK_EQ (c.project.video.start, PPQ * 4);
    CHECK_EQ (c.project.tracks.size(), before + 1);
    CHECK (! c.project.tracks.back().clips().empty());
    CHECK (c.zoneTab == ZoneTab::Video);
}
