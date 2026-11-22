#include "BattlePass.h"
#include "Editors.h"
#include "LuaTool.h"
#include "Song.h"
#include "ToolProcessor.h"
#include "ToolSession.h"
#include "Theme.h"

#include "lua.h"
#include "lauxlib.h"

namespace kal
{
using namespace juce;

namespace
{
//==============================================================================
// The `reaper` DAW Battle Pass calls, answered from Kalibre's song (0111). Only
// bp_watch.lua calls most of them; each means here what it means in REAPER.
// Projects, tracks, items and takes are Kalibre's ids as light userdata: a take
// is its clip, the master is track 0.

Controller* ctl = nullptr;
std::unique_ptr<Project> working;   // between Undo_BeginBlock and Undo_EndBlock
int undoDepth = 0;
juce::uint32 keysSeen = 0;

Project& song() { return working ? *working : ctl->project; }

void* handle (Id id) { return reinterpret_cast<void*> (static_cast<uintptr_t> (id)); }
Id idAt (lua_State* L, int i) { return lua_islightuserdata (L, i) ? static_cast<Id> (reinterpret_cast<uintptr_t> (lua_touserdata (L, i))) : 0; }
void pushHandle (lua_State* L, Id id) { lua_pushlightuserdata (L, handle (id)); }

Track* trackAt (lua_State* L, int i)
{
    const Id id = idAt (L, i);
    if (id == 0 || id == song().master.id) return &song().master;
    return song().track (id);
}

// A change to the song: inside an undo block, to the block's copy; outside, one undo step.
void change (const String& label, std::function<void (Project&)> f)
{
    if (working) f (*working);
    else ctl->apply (label, std::move (f));
}

double secondsAt (double qn) { return song().tempo.secondsAt ((Tick) std::llround (qn * PPQ)); }
double qnAt (double seconds) { return song().tempo.ticksAt (seconds) / (double) PPQ; }
Tick tickAt (double seconds) { return (Tick) std::llround (song().tempo.ticksAt (seconds)); }

// "VST3i: Serum (Xfer Records)", as REAPER names a plug-in.
std::string fxName (const PluginRef& p)
{
    std::string prefix = p.format == "VST3" ? "VST3" : p.format == "VST" ? "VST" : p.format == "AudioUnit" ? "AU"
                       : p.format == "CLAP" ? "CLAP" : "KAL";
    if (p.isInstrument) prefix += "i";
    return prefix + ": " + p.name + (p.manufacturer.empty() ? std::string() : " (" + p.manufacturer + ")");
}

std::vector<Clip*> allClips()
{
    std::vector<Clip*> out;
    for (auto& t : song().tracks)
        if (t.holdsClips())
            for (auto& c : t.clips()) out.push_back (&c);
    return out;
}

int r_EnumProjects (lua_State* L)
{
    if (luaL_optinteger (L, 1, -1) > 0) { lua_pushnil (L); return 1; }
    pushHandle (L, song().songId + 1);
    lua_pushstring (L, ctl->file.getFullPathName().toRawUTF8());
    return 2;
}
int r_GetProjectStateChangeCount (lua_State* L) { lua_pushinteger (L, ctl->changeCount); return 1; }
int r_GetPlayState (lua_State* L)
{
    lua_pushinteger (L, (ctl->engine.isPlaying() ? 1 : 0) | (ctl->engine.isRecording() ? 4 : 0));
    return 1;
}
int r_GetCursorPosition (lua_State* L) { lua_pushnumber (L, song().tempo.secondsAt (ctl->cursor)); return 1; }
int r_GetMousePosition (lua_State* L)
{
    const auto m = Desktop::getInstance().getMainMouseSource().getScreenPosition().roundToInt();
    lua_pushinteger (L, m.x);
    lua_pushinteger (L, m.y);
    return 2;
}
// js_ReaScriptAPI's: Kalibre is "REAPER" when it's the app in front, as the battle pass asks.
int r_GetMainHwnd (lua_State* L) { lua_pushlightuserdata (L, handle (1)); return 1; }
int r_JS_Window_GetForeground (lua_State* L) { lua_pushlightuserdata (L, handle (Process::isForegroundProcess() ? 1 : 2)); return 1; }
int r_JS_Window_GetParent (lua_State* L) { lua_pushnil (L); return 1; }
int r_JS_Window_GetTitle (lua_State* L) { lua_pushstring (L, ""); return 1; }
// The keyboard: a key down since the last look, standing for anything Kalibre counts as
// someone there (keys, clicks, MIDI played in, the mouse in its windows).
int r_JS_VKeys_GetState (lua_State* L)
{
    std::string keys (255, '\0');
    if (ctl->activityCount != keysSeen) keys[0] = '\1';
    keysSeen = ctl->activityCount;
    lua_pushlstring (L, keys.data(), keys.size());
    return 1;
}
int r_GetProjectName (lua_State* L)
{
    lua_pushstring (L, ctl->file.existsAsFile() ? ctl->file.getFileNameWithoutExtension().toRawUTF8() : "");
    return 1;
}
int r_GetResourcePath (lua_State* L) { lua_pushstring (L, BattlePass::resourceFolder().getFullPathName().toRawUTF8()); return 1; }
int r_RecursiveCreateDirectory (lua_State* L)
{
    lua_pushinteger (L, File (String::fromUTF8 (luaL_checkstring (L, 1))).createDirectory().wasOk() ? 1 : 0);
    return 1;
}
int r_file_exists (lua_State* L) { lua_pushboolean (L, File (String::fromUTF8 (luaL_checkstring (L, 1))).existsAsFile()); return 1; }

// Every plug-in Kalibre knows, and its own instruments.
int r_EnumInstalledFX (lua_State* L)
{
    const int i = (int) luaL_checkinteger (L, 1);
    std::vector<std::string> names;
    for (auto& d : ctl->engine.plugins().known().getTypes())
    {
        PluginRef r;
        r.format = d.pluginFormatName.toStdString();
        r.name = d.name.toStdString();
        r.manufacturer = d.manufacturerName.toStdString();
        r.isInstrument = d.isInstrument;
        names.push_back (fxName (r));
    }
    for (auto* own : { "Kalibre Synth", "Kalibre Sampler", "Kalibre Drums", "Trombolese" })
        names.push_back (std::string ("KALi: ") + own + " (Kalibre)");
    if (i < 0 || i >= (int) names.size()) { lua_pushboolean (L, 0); return 1; }
    lua_pushboolean (L, 1);
    lua_pushstring (L, names[(size_t) i].c_str());
    lua_pushstring (L, "");
    return 3;
}

int r_GetMasterTrack (lua_State* L) { pushHandle (L, song().master.id); return 1; }
int r_CountTracks (lua_State* L) { lua_pushinteger (L, (lua_Integer) song().tracks.size()); return 1; }
int r_GetTrack (lua_State* L)
{
    const auto i = luaL_checkinteger (L, 2);
    if (i < 0 || i >= (lua_Integer) song().tracks.size()) { lua_pushnil (L); return 1; }
    pushHandle (L, song().tracks[(size_t) i].id);
    return 1;
}
int r_GetTrackGUID (lua_State* L)
{
    auto* t = trackAt (L, 1);
    lua_pushstring (L, t ? ("{track-" + std::to_string (t->id) + "}").c_str() : "");
    return 1;
}
int r_TrackFX_GetCount (lua_State* L)
{
    auto* t = trackAt (L, 1);
    lua_pushinteger (L, t ? (lua_Integer) t->devices.size() : 0);
    return 1;
}
const Device* deviceAt (lua_State* L)
{
    auto* t = trackAt (L, 1);
    const auto i = luaL_checkinteger (L, 2);
    if (! t || i < 0 || i >= (lua_Integer) t->devices.size()) return nullptr;
    return &t->devices[(size_t) i];
}
int r_TrackFX_GetFXGUID (lua_State* L)
{
    auto* d = deviceAt (L);
    if (! d) { lua_pushnil (L); return 1; }
    lua_pushstring (L, ("{fx-" + std::to_string (d->id) + "}").c_str());
    return 1;
}
int r_TrackFX_GetFXName (lua_State* L)
{
    auto* d = deviceAt (L);
    lua_pushboolean (L, d != nullptr);
    lua_pushstring (L, d ? fxName (d->plugin).c_str() : "");
    return 2;
}
int r_TrackFX_GetNamedConfigParm (lua_State* L)
{
    auto* d = deviceAt (L);
    const bool ok = d != nullptr && std::string (luaL_checkstring (L, 3)) == "fx_name";
    lua_pushboolean (L, ok);
    lua_pushstring (L, ok ? fxName (d->plugin).c_str() : "");
    return 2;
}

int r_CountMediaItems (lua_State* L) { lua_pushinteger (L, (lua_Integer) allClips().size()); return 1; }
int r_GetMediaItem (lua_State* L)
{
    const auto clips = allClips();
    const auto i = luaL_checkinteger (L, 2);
    if (i < 0 || i >= (lua_Integer) clips.size()) { lua_pushnil (L); return 1; }
    pushHandle (L, clips[(size_t) i]->id);
    return 1;
}
Clip* clipAt (lua_State* L, int i) { return song().clip (idAt (L, i)); }
int r_GetActiveTake (lua_State* L)
{
    if (clipAt (L, 1)) lua_pushvalue (L, 1);
    else lua_pushnil (L);
    return 1;
}
int r_TakeIsMIDI (lua_State* L) { auto* c = clipAt (L, 1); lua_pushboolean (L, c && c->isMidi()); return 1; }
int r_MIDI_CountEvts (lua_State* L)
{
    auto* c = clipAt (L, 1);
    const auto notes = c ? (lua_Integer) c->midi.notes.size() : 0;
    lua_pushinteger (L, c ? 1 : 0);
    lua_pushinteger (L, notes);
    lua_pushinteger (L, c ? (lua_Integer) c->midi.events.size() : 0);
    lua_pushinteger (L, 0);
    return 4;
}
// GUID of an item; P_NAME of a take or a track (read or, with `set`, written).
int r_GetSetMediaItemInfo_String (lua_State* L)
{
    auto* c = clipAt (L, 1);
    const std::string what = luaL_checkstring (L, 2);
    if (c && what == "GUID") { lua_pushboolean (L, 1); lua_pushstring (L, ("{clip-" + std::to_string (c->id) + "}").c_str()); return 2; }
    if (c && what == "P_NAME") return lua_pushboolean (L, 1), lua_pushstring (L, c->name.c_str()), 2;
    lua_pushboolean (L, 0);
    lua_pushstring (L, "");
    return 2;
}
int r_GetSetMediaItemTakeInfo_String (lua_State* L)
{
    const Id id = idAt (L, 1);
    const std::string what = luaL_checkstring (L, 2);
    const std::string value = luaL_optstring (L, 3, "");
    if (what == "P_NAME" && lua_toboolean (L, 4) && song().clip (id))
        change ("Name a Clip", [id, value] (Project& p) { if (auto* c = p.clip (id)) c->name = value; });
    auto* c = song().clip (id);
    lua_pushboolean (L, c != nullptr);
    lua_pushstring (L, c ? c->name.c_str() : "");
    return 2;
}
int r_GetSetMediaTrackInfo_String (lua_State* L)
{
    auto* t = trackAt (L, 1);
    const Id id = t ? t->id : 0;
    const std::string what = luaL_checkstring (L, 2);
    const std::string value = luaL_optstring (L, 3, "");
    if (t && what == "P_NAME" && lua_toboolean (L, 4))
        change ("Name a Track", [id, value] (Project& p) { if (auto* tr = p.track (id)) tr->name = value; });
    t = song().track (id);
    lua_pushboolean (L, t != nullptr);
    lua_pushstring (L, t ? t->name.c_str() : "");
    return 2;
}
int r_CountProjectMarkers (lua_State* L)
{
    const auto m = (lua_Integer) song().markers.size(), r = (lua_Integer) song().sections.size();
    lua_pushinteger (L, m + r);
    lua_pushinteger (L, m);
    lua_pushinteger (L, r);
    return 3;
}

// Where a reward goes: the chosen track, if it can hold clips.
int r_GetSelectedTrack (lua_State* L)
{
    Id id = ! ctl->selectedTracks.empty() ? ctl->selectedTracks.front() : ctl->focusTrack;
    auto* t = song().track (id);
    if (t && t->holdsClips()) pushHandle (L, id);
    else lua_pushnil (L);
    return 1;
}
int r_GetLastTouchedTrack (lua_State* L) { lua_pushnil (L); return 1; }
int r_InsertTrackAtIndex (lua_State* L)
{
    change ("Add Track", [] (Project& p) { edit::addTrack (p, TrackKind::Track, "Battle Pass"); });
    return 0;
}

int r_TimeMap2_timeToQN (lua_State* L) { lua_pushnumber (L, qnAt (luaL_checknumber (L, 2))); return 1; }
int r_TimeMap2_QNToTime (lua_State* L) { lua_pushnumber (L, secondsAt (luaL_checknumber (L, 2))); return 1; }
int r_TimeMap_GetTimeSigAtTime (lua_State* L)
{
    const auto at = tickAt (luaL_checknumber (L, 2));
    const auto& m = song().tempo.meterAt (at);
    lua_pushinteger (L, m.numerator);
    lua_pushinteger (L, m.denominator);
    lua_pushnumber (L, song().tempo.bpmAt (at));
    return 3;
}

int r_Undo_BeginBlock (lua_State*)
{
    if (undoDepth++ == 0) working = std::make_unique<Project> (ctl->project);
    return 0;
}
int r_Undo_EndBlock (lua_State* L)
{
    if (undoDepth == 0) return 0;
    if (--undoDepth > 0) return 0;
    auto done = std::shared_ptr<Project> (working.release());
    ctl->apply (String::fromUTF8 (luaL_optstring (L, 1, "Battle Pass")), [done] (Project& p) { p = *done; });
    return 0;
}

int r_CreateNewMIDIItemInProj (lua_State* L)
{
    auto* t = trackAt (L, 1);
    if (! t || ! t->holdsClips()) { lua_pushnil (L); return 1; }
    const Id track = t->id;
    const Tick from = tickAt (luaL_checknumber (L, 2)), to = tickAt (luaL_checknumber (L, 3));
    Id made = 0;
    change ("Add a Clip", [&] (Project& p)
    {
        Clip c;
        c.kind = Clip::Kind::Midi;
        c.start = from;
        c.length = std::max<Tick> (1, to - from);
        made = edit::addClip (p, track, c);
    });
    if (made == 0) lua_pushnil (L);
    else pushHandle (L, made);
    return 1;
}
// A take's PPQ counts from its item's start, 960 to a quarter note, as Kalibre's ticks.
int r_MIDI_GetPPQPosFromProjQN (lua_State* L)
{
    auto* c = clipAt (L, 1);
    lua_pushnumber (L, luaL_checknumber (L, 2) * PPQ - (c ? (double) c->start : 0.0));
    return 1;
}
int r_MIDI_InsertNote (lua_State* L)
{
    const Id clip = idAt (L, 1);
    Note n;
    n.start = (Tick) std::llround (luaL_checknumber (L, 4));
    n.length = std::max<Tick> (1, (Tick) std::llround (luaL_checknumber (L, 5)) - n.start);
    n.channel = (int) luaL_optinteger (L, 6, 0);
    n.pitch = (int) luaL_checkinteger (L, 7);
    n.velocity = (int) luaL_optinteger (L, 8, 100);
    n.muted = lua_toboolean (L, 3) != 0;
    change ("Add a Note", [clip, n] (Project& p) { edit::addNote (p, clip, n); });
    lua_pushboolean (L, song().clip (clip) != nullptr);
    return 1;
}

int r_ColorToNative (lua_State* L)
{
    lua_pushinteger (L, (luaL_checkinteger (L, 1) & 255) | ((luaL_checkinteger (L, 2) & 255) << 8) | ((luaL_checkinteger (L, 3) & 255) << 16));
    return 1;
}
// A region is a section; a marker is a marker.
int r_AddProjectMarker2 (lua_State* L)
{
    const bool region = lua_toboolean (L, 2) != 0;
    const Tick a = tickAt (luaL_checknumber (L, 3)), b = tickAt (luaL_optnumber (L, 4, 0));
    const std::string name = luaL_optstring (L, 5, "");
    change (region ? "Add a Section" : "Add a Marker", [=] (Project& p)
    {
        if (region) song::addSection (p, name, a, std::max (b, a + 1));
        else
        {
            Marker m;
            m.id = p.newId();
            m.tick = a;
            m.name = name;
            p.markers.push_back (m);
        }
    });
    lua_pushinteger (L, 0);
    return 1;
}
int r_SetCurrentBPM (lua_State* L)
{
    const double bpm = luaL_checknumber (L, 2);
    change ("Set the Tempo", [bpm] (Project& p) { p.tempo.setTempoAt (0, std::clamp (bpm, TempoMap::minBpm, TempoMap::maxBpm)); });
    return 0;
}

// Starting with REAPER has no meaning here: the battle pass runs whenever Kalibre does.
int r_AddRemoveReaScript (lua_State* L) { lua_pushinteger (L, 0); return 1; }
int r_nil (lua_State* L) { lua_pushnil (L); return 1; }
int r_zero (lua_State* L) { lua_pushinteger (L, 0); return 1; }
int r_nothing (lua_State*) { return 0; }
// SWS's: open a file in its own app (the plug-in list).
int r_CF_ShellExecute (lua_State* L)
{
    const File f (String::fromUTF8 (luaL_checkstring (L, 1)));
    lua_pushboolean (L, f.startAsProcess());
    return 1;
}

void installReaper (lua_State* L)
{
    static const luaL_Reg fns[] = {
        { "EnumProjects", r_EnumProjects }, { "GetProjectStateChangeCount", r_GetProjectStateChangeCount },
        { "GetPlayState", r_GetPlayState }, { "GetCursorPosition", r_GetCursorPosition },
        { "GetMousePosition", r_GetMousePosition }, { "GetMainHwnd", r_GetMainHwnd },
        { "JS_Window_GetForeground", r_JS_Window_GetForeground }, { "JS_Window_GetParent", r_JS_Window_GetParent },
        { "JS_Window_GetTitle", r_JS_Window_GetTitle }, { "JS_VKeys_GetState", r_JS_VKeys_GetState },
        { "GetProjectName", r_GetProjectName }, { "GetResourcePath", r_GetResourcePath },
        { "RecursiveCreateDirectory", r_RecursiveCreateDirectory }, { "file_exists", r_file_exists },
        { "EnumInstalledFX", r_EnumInstalledFX },
        { "GetMasterTrack", r_GetMasterTrack }, { "CountTracks", r_CountTracks }, { "GetTrack", r_GetTrack },
        { "GetTrackGUID", r_GetTrackGUID }, { "TrackFX_GetCount", r_TrackFX_GetCount },
        { "TrackFX_GetFXGUID", r_TrackFX_GetFXGUID }, { "TrackFX_GetFXName", r_TrackFX_GetFXName },
        { "TrackFX_GetNamedConfigParm", r_TrackFX_GetNamedConfigParm },
        { "CountMediaItems", r_CountMediaItems }, { "GetMediaItem", r_GetMediaItem },
        { "GetActiveTake", r_GetActiveTake }, { "TakeIsMIDI", r_TakeIsMIDI }, { "MIDI_CountEvts", r_MIDI_CountEvts },
        { "GetSetMediaItemInfo_String", r_GetSetMediaItemInfo_String },
        { "GetSetMediaItemTakeInfo_String", r_GetSetMediaItemTakeInfo_String },
        { "GetSetMediaTrackInfo_String", r_GetSetMediaTrackInfo_String },
        { "CountProjectMarkers", r_CountProjectMarkers },
        { "GetSelectedTrack", r_GetSelectedTrack }, { "GetLastTouchedTrack", r_GetLastTouchedTrack },
        { "InsertTrackAtIndex", r_InsertTrackAtIndex },
        { "TimeMap2_timeToQN", r_TimeMap2_timeToQN }, { "TimeMap2_QNToTime", r_TimeMap2_QNToTime },
        { "TimeMap_GetTimeSigAtTime", r_TimeMap_GetTimeSigAtTime },
        { "Undo_BeginBlock", r_Undo_BeginBlock }, { "Undo_EndBlock", r_Undo_EndBlock },
        { "CreateNewMIDIItemInProj", r_CreateNewMIDIItemInProj },
        { "MIDI_GetPPQPosFromProjQN", r_MIDI_GetPPQPosFromProjQN }, { "MIDI_InsertNote", r_MIDI_InsertNote },
        { "ColorToNative", r_ColorToNative }, { "AddProjectMarker2", r_AddProjectMarker2 },
        { "SetCurrentBPM", r_SetCurrentBPM },
        { "AddRemoveReaScript", r_AddRemoveReaScript }, { "ReverseNamedCommandLookup", r_nil },
        { "NamedCommandLookup", r_zero }, { "Main_OnCommand", r_nothing },
        { "CF_ShellExecute", r_CF_ShellExecute },
        { "PreventUIRefresh", r_nothing }, { "TrackList_AdjustWindows", r_nothing }, { "UpdateArrange", r_nothing },
        { "MIDI_Sort", r_nothing }, { "RefreshToolbar", r_nothing },
        { nullptr, nullptr } };
    luaL_setfuncs (L, fns, 0);
}

//==============================================================================
// Its window: the script's, drawn by the tool's own editor.
class Window : public DocumentWindow, private KeyListener
{
public:
    Window (AudioProcessorEditor* e, std::function<void()> closed)
        : DocumentWindow ("DAW Battle Pass", theme::ground, DocumentWindow::closeButton), onClosed (std::move (closed))
    {
        setUsingNativeTitleBar (true);
        setContentNonOwned (e, true);
        editor.reset (e);
        setResizable (true, false);
        if (auto* k = e->getConstrainer()) setConstrainer (k);
        e->addKeyListener (this);
    }
    ~Window() override { empty(); }
    void empty()
    {
        if (editor) editor->removeKeyListener (this);
        clearContentComponent();
        editor.reset();   // the script's window is told first (0056)
    }
    void closeButtonPressed() override { MessageManager::callAsync ([f = onClosed] { if (f) f(); }); }

private:
    editors::Owned editor;
    std::function<void()> onClosed;
    // ⌘W closes it; Esc is the game's (it skips a celebration, and never closes the window).
    bool keyPressed (const KeyPress& k, Component*) override
    {
        if (k == KeyPress ('w', ModifierKeys::commandModifier, 0)) { closeButtonPressed(); return true; }
        return false;
    }
};
} // namespace

//==============================================================================
File& BattlePass::resourceFolder()
{
    static File f = File::getSpecialLocation (File::userApplicationDataDirectory)
                   #if JUCE_MAC
                       .getChildFile ("Application Support")
                   #endif
                       .getChildFile ("Kalibre");
    return f;
}

File& BattlePass::reaperFolder()
{
    static File f = File::getSpecialLocation (File::userApplicationDataDirectory)
                   #if JUCE_MAC
                       .getChildFile ("Application Support")
                   #endif
                       .getChildFile ("REAPER");
    return f;
}

// The musician played the battle pass in REAPER first: their progress comes too,
// the first time (never over Kalibre's own).
void BattlePass::bringOverReapersSave()
{
    const auto ours = resourceFolder().getChildFile ("Data").getChildFile ("DAW Battle Pass");
    const auto theirs = reaperFolder().getChildFile ("Data").getChildFile ("DAW Battle Pass");
    if (ours.getChildFile ("progress.lua").existsAsFile() || ! theirs.getChildFile ("progress.lua").existsAsFile()) return;
    ours.createDirectory();
    for (auto* name : { "progress.lua", "progress.lua.bak", "My Plugins.txt" })
        if (theirs.getChildFile (name).existsAsFile()) theirs.getChildFile (name).copyFileTo (ours.getChildFile (name));
}

BattlePass::BattlePass (Controller& controller) : c (controller)
{
    ctl = &c;
    working.reset();
    undoDepth = 0;
    keysSeen = c.activityCount;
    battlePassSpec().extendReaper = installReaper;
    bringOverReapersSave();
    tool = std::make_unique<tools::ToolProcessor> (battlePassSpec());
    // The size the script asks for (SetNextWindowSize 920 x 780), until it's resized.
    tool->editorWidth = 920;
    tool->editorHeight = 780;
    startTimerHz (4);
}

BattlePass::~BattlePass()
{
    stopTimer();
    shownIn.reset();
    tool.reset();   // the script's atexit: it saves
    if (ctl == &c) ctl = nullptr;
    working.reset();
}

void BattlePass::show()
{
    if (! shownIn)
    {
        shownIn = std::make_unique<Window> (editors::open (*tool), [this] { hide(); });
        if (auto* top = TopLevelWindow::getActiveTopLevelWindow())
            shownIn->centreAroundComponent (top, shownIn->getWidth(), shownIn->getHeight());
        else shownIn->centreWithSize (shownIn->getWidth(), shownIn->getHeight());
    }
    shownIn->setVisible (true);
    shownIn->toFront (true);
}

// Its window waits a little before it's deleted (0115); the game goes on unseen.
void BattlePass::hide()
{
    if (auto* w = dynamic_cast<Window*> (shownIn.get())) { w->setVisible (false); w->empty(); }
    editors::retire (std::move (shownIn));
}
bool BattlePass::isShown() const { return shownIn != nullptr && shownIn->isVisible(); }

// Shut, it goes on four times a second: the clock, the scan, its saving.
void BattlePass::timerCallback()
{
    if (! isShown()) frameNow();
}

void BattlePass::frameNow()
{
    // At the screen's density, as its window draws: its fonts are made once, not again on opening.
    float scale = 1.0f;
    if (auto* d = Desktop::getInstance().getDisplays().getPrimaryDisplay()) scale = jmax (1.0f, (float) d->scale);
    if (auto* s = tool->session())
        s->frame ((float) (tool->editorWidth > 0 ? tool->editorWidth : 920), (float) (tool->editorHeight > 0 ? tool->editorHeight : 780), scale);
}

String BattlePass::error() const { return tool->session() ? tool->session()->error() : String ("no session"); }
String BattlePass::run (const String& lua) { return tool->session() ? tool->session()->run (lua) : String ("no session"); }

} // namespace kal
