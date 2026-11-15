#include "Check.h"
#include "Edit.h"
#include "LuaHost.h"

using namespace kal;

TEST ("script: makes tracks, clips and notes")
{
    Project p;
    const auto r = script::run (p, R"(
        local t = kalibre.addTrack("Lead")
        local c = kalibre.addMidiClip(t, 0, kalibre.ppq * 4)
        for i = 0, 3 do kalibre.addNote(c, { start = i * kalibre.ppq, length = kalibre.ppq, pitch = 60 + i }) end
        kalibre.setTempo(0, 96)
        print(#kalibre.notes(c))
    )", "test");
    CHECK (r.ok);
    CHECK (r.changed);
    CHECK_EQ (r.printed, std::string ("4\n"));
    CHECK_EQ (p.tracks.size(), size_t (1));
    CHECK_EQ (p.tempo.bpmAt (0), 96.0);
}

TEST ("script: a failing script changes nothing")
{
    Project p;
    edit::addTrack (p, TrackKind::Track, "Keep");
    const auto before = p;
    const auto r = script::run (p, "kalibre.addTrack('Gone'); error('stop')", "test");
    CHECK (! r.ok);
    CHECK (r.error.find ("stop") != std::string::npos);
    CHECK (p == before);
}

TEST ("script: a runaway script is stopped")
{
    Project p;
    const auto r = script::run (p, "while true do end", "test", {}, 0.2);
    CHECK (! r.ok);
    CHECK (r.error.find ("too long") != std::string::npos);
}

TEST ("script: sees the selection and the scale")
{
    Project p;
    const Id t = edit::addTrack (p, TrackKind::Track, "Keys");
    const Id c = edit::addMidiClip (p, t, 0, PPQ * 4);
    edit::addNote (p, c, { 0, 0, PPQ, 61, 100 });
    script::Context ctx;
    ctx.selectedClip = c;
    const auto r = script::run (p, R"(
        kalibre.setScale("D", "Major")
        local sel = kalibre.selection()
        for _, n in ipairs(kalibre.notes(sel.clip)) do
            if not kalibre.inScale(n.pitch) then print("out") else print("in") end
        end
        kalibre.transpose(sel.clip, 12)
    )", "test", ctx);
    CHECK (r.ok);
    CHECK_EQ (r.printed, std::string ("in\n"));
    CHECK_EQ (p.clip (c)->midi.notes[0].pitch, 73);
}

#include <filesystem>
#include <fstream>
#include <sstream>

TEST ("script: every example script runs")
{
    namespace fs = std::filesystem;
    int ran = 0;
    for (auto& entry : fs::directory_iterator (fs::path (KALIBRE_SOURCE_DIR) / "Scripts"))
    {
        if (entry.path().extension() != ".lua") continue;
        std::ifstream in (entry.path());
        std::stringstream code;
        code << in.rdbuf();
        Project p;
        const Id t = edit::addTrack (p, TrackKind::Track, "Keys");
        const Id c = edit::addMidiClip (p, t, 0, PPQ * 4);
        script::Context ctx;
        ctx.selectedClip = c;
        for (int pitch : { 60, 63, 67 }) ctx.selectedNotes.insert (edit::addNote (p, c, { 0, 0, PPQ, pitch, 90 }));
        std::string said;
        ctx.message = [&said] (const std::string& m) { said = m; };
        const auto r = script::run (p, code.str(), entry.path().filename().string(), ctx);
        if (! r.ok) std::printf ("  %s: %s\n", entry.path().filename().string().c_str(), r.error.c_str());
        CHECK (r.ok);
        CHECK (! said.empty());
        ++ran;
    }
    CHECK (ran >= 4);
}
