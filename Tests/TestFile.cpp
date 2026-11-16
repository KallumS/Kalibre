#include "Check.h"
#include "Edit.h"
#include "ProjectFile.h"

using namespace kal;

TEST ("file: a project survives saving and loading")
{
    Project p;
    p.name = "Song";
    p.tempo.setTempoAt (PPQ * 8, 90, true);
    p.tempo.setMeterAt (4, 6, 8);
    const Id f = edit::addTrack (p, TrackKind::Folder, "Band");
    const Id t = edit::addTrack (p, TrackKind::Track, "Bass", 0, f);
    const Id c = edit::addMidiClip (p, t, 0, PPQ * 4);
    edit::addNote (p, c, { 0, 0, PPQ, 40, 99 });
    edit::newVersion (p, t);
    edit::addDevice (p, t, { "CLAP", "/a.clap", "A", "M", 7 }, DeviceRole::AudioFx);
    p.track (t)->delayMs = -125;
    p.scale.root = 9;
    p.scale.scale = 1;
    p.markers.push_back ({ p.newId(), PPQ * 16, "Chorus" });
    const auto text = file::save (p);
    std::string error;
    const auto q = file::load (text, &error);
    CHECK (q.has_value());
    CHECK (error.empty());
    CHECK (*q == p);
}

TEST ("file: not a project, a newer project, missing fields")
{
    std::string error;
    CHECK (! file::load ("{\"hello\":1}", &error));
    CHECK (! error.empty());
    CHECK (! file::load ("{\"kalibre\":99}", &error));
    CHECK (! file::load ("not json", &error));
    const auto q = file::load ("{\"kalibre\":1,\"tracks\":[{\"id\":5,\"name\":\"Old\"}]}", &error);
    CHECK (q.has_value());
    CHECK_EQ (q->tracks.front().name, std::string ("Old"));
    CHECK_EQ (q->tracks.front().versions.size(), size_t (1));
    CHECK (q->nextId > 5);
}

bool roundTrips (const Project& p)
{
    const auto q = file::load (file::save (p));
    return q && *q == p;
}
