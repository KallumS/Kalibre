// The activity log (decision 0135): what was done in a song, kept beside it
// as text, for record labels that ask for evidence it was made by hand.
#include "Check.h"
#include "Controller.h"
#include "Edit.h"
#include "Engine.h"
#include "PluginManager.h"
#include "AgentTools.h"
#include "MainComponent.h"
#include "Theme.h"

using namespace kal;
using namespace juce;

namespace
{
File songFolder (const String& name)
{
    auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("kalibre-activity-" + name);
    dir.deleteRecursively();
    dir.createDirectory();
    return dir;
}
int count (const String& text, const String& what)
{
    int n = 0;
    for (int at = text.indexOf (what); at >= 0; at = text.indexOf (at + 1, what)) ++n;
    return n;
}
} // namespace

TEST ("activity log: saving writes what was done beside the song, each edit, undo and redo by name, with when; saving again adds only what's new")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    const bool was = c.keepActivityLog;
    c.setKeepActivityLog (true);
    c.newProject();
    c.addTrack (TrackKind::Track, "Bass");
    c.undo();
    c.redo();
    const auto song = songFolder ("save").getChildFile ("My Song.kalibre");
    CHECK (c.saveAs (song));
    const auto log = song.getSiblingFile ("My Song Activity Log.txt");
    CHECK (log.existsAsFile());
    auto text = log.loadFileAsString();
    CHECK (text.contains ("Activity log of \"My Song\""));
    CHECK (text.contains ("New       "));
    CHECK (text.contains ("Edit      Add Track"));
    CHECK (text.contains ("Undo      Add Track"));
    CHECK (text.contains ("Redo      Add Track"));
    CHECK (text.contains ("Save      Saved My Song"));
    // Each line starts with when: the date and time.
    const auto today = Time::getCurrentTime().formatted ("%Y-%m-%d");
    CHECK (count (text, today + " ") >= 5);

    // Another edit, saved again: added after, nothing written twice.
    c.addTrack (TrackKind::Track, "Drums");
    CHECK (c.save());
    text = log.loadFileAsString();
    CHECK_EQ (count (text, "Activity log of"), 1);
    CHECK_EQ (count (text, "Undo      Add Track"), 1);
    CHECK_EQ (count (text, "Edit      Add Track"), 2);   // Bass, then Drums
    CHECK (text.lastIndexOf ("Edit      Add Track") > text.indexOf ("Save      Saved My Song"));
    c.setKeepActivityLog (was);
}

TEST ("activity log: an AI agent's edits say so; playing and stopping are there; an export's progress isn't, line by line")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    const bool was = c.keepActivityLog;
    c.setKeepActivityLog (true);
    c.newProject();
    theme::LookAndFeel look;
    LookAndFeel::setDefaultLookAndFeel (&look);
    auto window = std::make_unique<MainComponent> (c);   // it shows an export's progress
    window->setSize (1440, 900);
    auto server = makeAgentServer (c);
    server->handle (R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"create_track","arguments":{"name":"Strings"}}})");
    c.play();
    c.stop();
    CHECK (c.onExportProgress != nullptr);
    for (int p = 0; p < 5; ++p) if (c.onExportProgress) c.onExportProgress (p / 4.0f);
    const auto song = songFolder ("agent").getChildFile ("Agent Song.kalibre");
    CHECK (c.saveAs (song));
    const auto text = song.getSiblingFile ("Agent Song Activity Log.txt").loadFileAsString();
    CHECK (text.contains ("AI agent  Used create_track"));
    CHECK (text.contains ("AI agent  Add Track"));
    CHECK (text.contains ("Play      "));
    CHECK (text.contains ("Stop      "));
    CHECK (! text.contains ("%"));
    window.reset();
    LookAndFeel::setDefaultLookAndFeel (nullptr);
    c.setKeepActivityLog (was);
}

TEST ("activity log: switched off, nothing is kept or written; switched on again, the log says it was off")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    const bool was = c.keepActivityLog;
    c.newProject();
    const auto song = songFolder ("off").getChildFile ("Quiet Song.kalibre");
    c.setKeepActivityLog (false);
    c.addTrack (TrackKind::Track, "Keys");
    CHECK (c.saveAs (song));
    const auto log = song.getSiblingFile ("Quiet Song Activity Log.txt");
    CHECK (! log.existsAsFile());
    c.setKeepActivityLog (true);
    c.addTrack (TrackKind::Track, "Pad");
    CHECK (c.save());
    const auto text = log.loadFileAsString();
    CHECK (! text.contains ("Keys"));
    CHECK (text.contains ("Log       "));
    CHECK (text.contains ("Edit      Add Track"));
    c.setKeepActivityLog (was);
}

TEST ("activity log: Save As to a new name carries the song's log so far with it")
{
    PluginManager plugins;
    Engine engine (plugins);
    Controller c (engine);
    const bool was = c.keepActivityLog;
    c.setKeepActivityLog (true);
    c.newProject();
    const auto dir = songFolder ("saveas");
    c.addTrack (TrackKind::Track, "Lead");
    CHECK (c.saveAs (dir.getChildFile ("First.kalibre")));
    c.addTrack (TrackKind::Track, "Choir");
    CHECK (c.saveAs (dir.getChildFile ("Second.kalibre")));
    const auto text = dir.getChildFile ("Second Activity Log.txt").loadFileAsString();
    CHECK (text.contains ("Saved First"));
    CHECK (text.contains ("Saved Second"));
    CHECK_EQ (count (text, "Edit      Add Track"), 2);
    c.setKeepActivityLog (was);
}
