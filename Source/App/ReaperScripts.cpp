#include "ReaperScripts.h"
#include "Controller.h"
#include "EmbeddedLua.h"
#include "JsfxDevice.h"

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

using namespace juce;

namespace kal
{

namespace
{
// REAPER's objects as light userdata: a track, an item (a clip) and its take
// (the same clip), each an Id with what it is in the low bits.
enum Kind { trackKind = 1, itemKind = 2, takeKind = 3 };
void push (lua_State* L, Id id, Kind k) { lua_pushlightuserdata (L, reinterpret_cast<void*> ((uintptr_t) ((id << 2) | (Id) k))); }
Id idOf (lua_State* L, int i, Kind k)
{
    if (! lua_islightuserdata (L, i)) return 0;
    const auto v = (Id) reinterpret_cast<uintptr_t> (lua_touserdata (L, i));
    return (v & 3) == (Id) k ? v >> 2 : 0;
}

ReaperScript& self (lua_State* L) { return *static_cast<ReaperScript*> (lua_touserdata (L, lua_upvalueindex (1))); }

// REAPER's tracks: every track but the master, in order; the master apart.
std::vector<Id> tracksOf (const Project& p)
{
    std::vector<Id> out;
    for (auto& t : p.tracks) if (t.kind != TrackKind::Master) out.push_back (t.id);
    return out;
}
Id masterOf (const Project& p)
{
    for (auto& t : p.tracks) if (t.kind == TrackKind::Master) return t.id;
    return 0;
}
int indexOf (const Project& p, Id track)
{
    const auto list = tracksOf (p);
    const auto it = std::find (list.begin(), list.end(), track);
    return it == list.end() ? -1 : (int) (it - list.begin());
}

// The selected items: the chosen clips, from the earliest.
std::vector<Id> chosenClips (Controller& c)
{
    std::vector<Id> ids (c.selectedClips.begin(), c.selectedClips.end());
    if (ids.empty() && c.selectedClip) ids.push_back (c.selectedClip);
    ids.erase (std::remove_if (ids.begin(), ids.end(), [&c] (Id id) { return c.project.clip (id) == nullptr; }), ids.end());
    std::stable_sort (ids.begin(), ids.end(), [&c] (Id a, Id b) { return c.project.clip (a)->start < c.project.clip (b)->start; });
    return ids;
}

const Device* deviceAt (Controller& c, Id track, int fx)
{
    const auto* t = c.project.track (track);
    if (! t || fx < 0 || fx >= (int) t->devices.size()) return nullptr;
    return &t->devices[(size_t) fx];
}
AudioProcessorParameter* paramAt (Controller& c, Id track, int fx, int param)
{
    const auto* d = deviceAt (c, track, fx);
    auto* inst = d ? c.engine.instance (d->id) : nullptr;
    if (! inst || param < 0 || param >= inst->getParameters().size()) return nullptr;
    return inst->getParameters()[param];
}

double seconds (Controller& c, Tick t) { return c.project.tempo.secondsAt (t); }

//==============================================================================
int r_GetMasterTrack (lua_State* L) { push (L, masterOf (self (L).c.project), trackKind); return 1; }
int r_CountTracks (lua_State* L) { lua_pushinteger (L, (lua_Integer) tracksOf (self (L).c.project).size()); return 1; }
int r_GetTrack (lua_State* L)
{
    const auto list = tracksOf (self (L).c.project);
    const auto i = luaL_checkinteger (L, 2);
    if (i < 0 || i >= (lua_Integer) list.size()) { lua_pushnil (L); return 1; }
    push (L, list[(size_t) i], trackKind);
    return 1;
}
int r_GetTrackName (lua_State* L)
{
    const auto* t = self (L).c.project.track (idOf (L, 1, trackKind));
    lua_pushboolean (L, t != nullptr);
    lua_pushstring (L, t ? t->name.c_str() : "");
    return 2;
}
int r_GetMediaTrackInfo_Value (lua_State* L)
{
    auto& s = self (L);
    const Id t = idOf (L, 1, trackKind);
    const String what (luaL_checkstring (L, 2));
    double v = 0;
    if (what == "IP_TRACKNUMBER") v = t == masterOf (s.c.project) ? -1 : indexOf (s.c.project, t) + 1;
    lua_pushnumber (L, v);
    return 1;
}
int r_CountSelectedTracks (lua_State* L) { lua_pushinteger (L, (lua_Integer) self (L).c.selectedTracks.size()); return 1; }
int r_GetSelectedTrack (lua_State* L)
{
    auto& sel = self (L).c.selectedTracks;
    const auto i = luaL_checkinteger (L, 2);
    if (i < 0 || i >= (lua_Integer) sel.size()) { lua_pushnil (L); return 1; }
    push (L, sel[(size_t) i], trackKind);
    return 1;
}

int r_CountSelectedMediaItems (lua_State* L) { lua_pushinteger (L, (lua_Integer) chosenClips (self (L).c).size()); return 1; }
int r_GetSelectedMediaItem (lua_State* L)
{
    const auto list = chosenClips (self (L).c);
    const auto i = luaL_checkinteger (L, 2);
    if (i < 0 || i >= (lua_Integer) list.size()) { lua_pushnil (L); return 1; }
    push (L, list[(size_t) i], itemKind);
    return 1;
}
int r_GetActiveTake (lua_State* L) { push (L, idOf (L, 1, itemKind), takeKind); return 1; }
int r_TakeIsMIDI (lua_State* L)
{
    const auto* clip = self (L).c.project.clip (idOf (L, 1, takeKind));
    lua_pushboolean (L, clip && clip->isMidi());
    return 1;
}
int r_GetMediaItem_Track (lua_State* L)
{
    const Track* owner = nullptr;
    self (L).c.project.clip (idOf (L, 1, itemKind), &owner);
    if (owner) push (L, owner->id, trackKind); else lua_pushnil (L);
    return 1;
}
int r_GetMediaItemTake_Source (lua_State* L) { lua_pushvalue (L, 1); return 1; }
int r_GetMediaSourceLength (lua_State* L)
{
    // In quarter notes: the loop's length, or the content's.
    const auto* clip = self (L).c.project.clip (idOf (L, 1, takeKind));
    const double qn = clip ? (double) (clip->loopLength > 0 ? clip->loopLength : clip->offset + clip->length) / PPQ : 0;
    lua_pushnumber (L, qn);
    lua_pushboolean (L, 1);
    return 2;
}
int r_GetMediaItemInfo_Value (lua_State* L)
{
    auto& s = self (L);
    const auto* clip = s.c.project.clip (idOf (L, 1, itemKind));
    const String what (luaL_checkstring (L, 2));
    double v = 0;
    if (clip)
    {
        if (what == "D_POSITION") v = seconds (s.c, clip->start);
        else if (what == "D_LENGTH") v = seconds (s.c, clip->end()) - seconds (s.c, clip->start);
        else if (what == "B_LOOPSRC") v = clip->loopLength > 0 ? 1 : 0;
        else if (what == "B_MUTE") v = clip->muted ? 1 : 0;
    }
    lua_pushnumber (L, v);
    return 1;
}
int r_GetMediaItemTakeInfo_Value (lua_State* L)
{
    const String what (luaL_checkstring (L, 2));
    lua_pushnumber (L, what == "D_PLAYRATE" ? 1.0 : 0.0);
    return 1;
}
int r_GetTakeName (lua_State* L)
{
    const auto* clip = self (L).c.project.clip (idOf (L, 1, takeKind));
    lua_pushstring (L, clip ? clip->name.c_str() : "");
    return 1;
}
int r_GetSetMediaItemTakeInfo_String (lua_State* L)
{
    const auto* clip = self (L).c.project.clip (idOf (L, 1, takeKind));
    lua_pushboolean (L, clip != nullptr);
    lua_pushstring (L, clip ? clip->name.c_str() : "");
    return 2;
}
int r_MIDI_CountEvts (lua_State* L)
{
    const auto* clip = self (L).c.project.clip (idOf (L, 1, takeKind));
    const int notes = clip ? (int) clip->midi.notes.size() : 0;
    const int ccs = clip ? (int) clip->midi.events.size() : 0;
    lua_pushinteger (L, notes + ccs);
    lua_pushinteger (L, notes);
    lua_pushinteger (L, ccs);
    lua_pushinteger (L, 0);
    return 4;
}
int r_MIDI_GetNote (lua_State* L)
{
    auto& s = self (L);
    const auto* clip = s.c.project.clip (idOf (L, 1, takeKind));
    const auto i = luaL_checkinteger (L, 2);
    if (! clip || i < 0 || i >= (lua_Integer) clip->midi.notes.size()) { lua_pushboolean (L, 0); return 1; }
    const auto& n = clip->midi.notes[(size_t) i];
    // The take's PPQ is the clip's own ticks, 960 a quarter as REAPER's are.
    lua_pushboolean (L, 1);
    lua_pushboolean (L, s.c.selectedNotes.count (n.id) > 0);
    lua_pushboolean (L, n.muted);
    lua_pushnumber (L, (double) n.start);
    lua_pushnumber (L, (double) n.end());
    lua_pushinteger (L, n.channel);
    lua_pushinteger (L, n.pitch);
    lua_pushinteger (L, n.velocity);
    return 8;
}
int r_MIDI_GetProjQNFromPPQPos (lua_State* L)
{
    const auto* clip = self (L).c.project.clip (idOf (L, 1, takeKind));
    const double ppq = luaL_checknumber (L, 2);
    lua_pushnumber (L, clip ? ((double) (clip->start - clip->offset) + ppq) / PPQ : 0.0);
    return 1;
}
int r_GetNumTakeMarkers (lua_State* L) { lua_pushinteger (L, 0); return 1; }
int r_GetTakeMarker (lua_State* L) { lua_pushnumber (L, -1); return 1; }

int r_CountProjectMarkers (lua_State* L)
{
    const auto n = (lua_Integer) self (L).c.project.markers.size();
    lua_pushinteger (L, n);
    lua_pushinteger (L, n);
    lua_pushinteger (L, 0);
    return 3;
}
int enumMarker (lua_State* L, int index)
{
    auto& s = self (L);
    const auto& m = s.c.project.markers;
    if (index < 0 || index >= (int) m.size()) { lua_pushinteger (L, 0); return 1; }
    lua_pushinteger (L, index + 1);
    lua_pushboolean (L, 0);
    lua_pushnumber (L, seconds (s.c, m[(size_t) index].tick));
    lua_pushnumber (L, 0);
    lua_pushstring (L, m[(size_t) index].name.c_str());
    lua_pushinteger (L, index + 1);
    return 6;
}
int r_EnumProjectMarkers (lua_State* L) { return enumMarker (L, (int) luaL_checkinteger (L, 1)); }
int r_EnumProjectMarkers3 (lua_State* L) { return enumMarker (L, (int) luaL_checkinteger (L, 2)); }

int r_TimeMap2_timeToQN (lua_State* L) { lua_pushnumber (L, self (L).c.project.tempo.ticksAt (luaL_checknumber (L, 2)) / PPQ); return 1; }
int r_TimeMap2_QNToTime (lua_State* L) { lua_pushnumber (L, self (L).c.project.tempo.secondsAt ((Tick) std::llround (luaL_checknumber (L, 2) * PPQ))); return 1; }
int r_TimeMap_GetTimeSigAtTime (lua_State* L)
{
    const auto& tempo = self (L).c.project.tempo;
    const Tick at = tempo.tickAt (luaL_checknumber (L, 2));
    const auto& m = tempo.meterAt (at);
    lua_pushinteger (L, m.numerator);
    lua_pushinteger (L, m.denominator);
    lua_pushnumber (L, tempo.bpmAt (at));
    return 3;
}
int r_Master_GetTempo (lua_State* L) { lua_pushnumber (L, self (L).c.project.tempo.bpmAt (0)); return 1; }

//==============================================================================
int r_TrackFX_GetCount (lua_State* L)
{
    const auto* t = self (L).c.project.track (idOf (L, 1, trackKind));
    lua_pushinteger (L, t ? (lua_Integer) t->devices.size() : 0);
    return 1;
}
int r_TrackFX_GetNumParams (lua_State* L)
{
    auto& s = self (L);
    const auto* d = deviceAt (s.c, idOf (L, 1, trackKind), (int) luaL_checkinteger (L, 2));
    auto* inst = d ? s.c.engine.instance (d->id) : nullptr;
    lua_pushinteger (L, inst ? inst->getParameters().size() : 0);
    return 1;
}
int r_TrackFX_GetParamName (lua_State* L)
{
    auto* p = paramAt (self (L).c, idOf (L, 1, trackKind), (int) luaL_checkinteger (L, 2), (int) luaL_checkinteger (L, 3));
    lua_pushboolean (L, p != nullptr);
    lua_pushstring (L, p ? p->getName (256).toRawUTF8() : "");
    return 2;
}
int r_TrackFX_GetParam (lua_State* L)
{
    auto* p = paramAt (self (L).c, idOf (L, 1, trackKind), (int) luaL_checkinteger (L, 2), (int) luaL_checkinteger (L, 3));
    double v = 0, lo = 0, hi = 1;
    if (p && ! jsfxSliderValue (*p, v, lo, hi)) v = p->getValue();
    lua_pushnumber (L, v);
    lua_pushnumber (L, lo);
    lua_pushnumber (L, hi);
    return 3;
}
int r_TrackFX_SetParam (lua_State* L)
{
    auto* p = paramAt (self (L).c, idOf (L, 1, trackKind), (int) luaL_checkinteger (L, 2), (int) luaL_checkinteger (L, 3));
    const double v = luaL_checknumber (L, 4);
    if (p && ! setJsfxSlider (*p, v)) p->setValue ((float) jlimit (0.0, 1.0, v));
    lua_pushboolean (L, p != nullptr);
    return 1;
}
int r_TrackFX_SetParamNormalized (lua_State* L)
{
    auto* p = paramAt (self (L).c, idOf (L, 1, trackKind), (int) luaL_checkinteger (L, 2), (int) luaL_checkinteger (L, 3));
    if (p) p->setValue ((float) jlimit (0.0, 1.0, luaL_checknumber (L, 4)));
    lua_pushboolean (L, p != nullptr);
    return 1;
}
// The device the script was run from is REAPER's focused FX.
int r_GetTouchedOrFocusedFX (lua_State* L)
{
    auto& s = self (L);
    Track* owner = nullptr;
    const auto* d = s.focus ? s.c.project.device (s.focus, &owner) : nullptr;
    if (! d || ! owner) { lua_pushboolean (L, 0); return 1; }
    int fx = 0;
    for (size_t i = 0; i < owner->devices.size(); ++i) if (owner->devices[i].id == d->id) fx = (int) i;
    lua_pushboolean (L, 1);
    lua_pushinteger (L, owner->kind == TrackKind::Master ? -1 : indexOf (s.c.project, owner->id));
    lua_pushinteger (L, -1);
    lua_pushinteger (L, -1);
    lua_pushinteger (L, fx);
    lua_pushinteger (L, -1);
    return 6;
}
int r_GetFocusedFX (lua_State* L)
{
    auto& s = self (L);
    Track* owner = nullptr;
    const auto* d = s.focus ? s.c.project.device (s.focus, &owner) : nullptr;
    if (! d || ! owner) { lua_pushinteger (L, 0); return 1; }
    int fx = 0;
    for (size_t i = 0; i < owner->devices.size(); ++i) if (owner->devices[i].id == d->id) fx = (int) i;
    lua_pushinteger (L, 1);
    lua_pushinteger (L, owner->kind == TrackKind::Master ? 0 : indexOf (s.c.project, owner->id) + 1);
    lua_pushinteger (L, 0);
    lua_pushinteger (L, fx);
    return 4;
}

//==============================================================================
int r_gmem_attach (lua_State* L) { self (L).gmemName = String::fromUTF8 (luaL_checkstring (L, 1)); return 0; }
int r_gmem_read (lua_State* L)
{
    const auto* v = jsfxGmem (self (L).gmemName, (unsigned int) jmax<lua_Integer> (0, luaL_checkinteger (L, 1)));
    lua_pushnumber (L, v ? *v : 0.0);
    return 1;
}
int r_gmem_write (lua_State* L)
{
    if (auto* v = jsfxGmem (self (L).gmemName, (unsigned int) jmax<lua_Integer> (0, luaL_checkinteger (L, 1)))) *v = luaL_checknumber (L, 2);
    return 0;
}

int r_defer (lua_State* L)
{
    luaL_checktype (L, 1, LUA_TFUNCTION);
    lua_pushvalue (L, 1);
    self (L).deferred.push_back (luaL_ref (L, LUA_REGISTRYINDEX));
    return 0;
}
int r_atexit (lua_State* L)
{
    luaL_checktype (L, 1, LUA_TFUNCTION);
    lua_pushvalue (L, 1);
    self (L).atExit.push_back (luaL_ref (L, LUA_REGISTRYINDEX));
    return 0;
}
int r_time_precise (lua_State* L) { lua_pushnumber (L, Time::getMillisecondCounterHiRes() / 1000.0); return 1; }
// A message box is a message in the message line.
int r_MB (lua_State* L)
{
    const String text = String::fromUTF8 (luaL_checkstring (L, 1)).replace ("\n\n", " ").replace ("\n", " ");
    self (L).c.say (text, true);
    lua_pushinteger (L, 1);
    return 1;
}
int r_cancelled (lua_State* L) { lua_pushboolean (L, 0); return 1; }
int r_MarkProjectDirty (lua_State* L) { self (L).c.dirty = true; return 0; }
int r_nothing (lua_State*) { return 0; }
File resourceFolder()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
   #if JUCE_MAC
        .getChildFile ("Application Support")
   #endif
        .getChildFile ("Kalibre").getChildFile ("JSFX");
}
int r_GetResourcePath (lua_State* L) { lua_pushstring (L, resourceFolder().getFullPathName().toRawUTF8()); return 1; }
int r_RecursiveCreateDirectory (lua_State* L)
{
    lua_pushinteger (L, File (String::fromUTF8 (luaL_checkstring (L, 1))).createDirectory().wasOk() ? 1 : 0);
    return 1;
}
int r_get_action_context (lua_State* L)
{
    lua_pushboolean (L, 0);
    lua_pushstring (L, ("<embedded>/" + self (L).path).toRawUTF8());
    lua_pushinteger (L, 0);
    lua_pushinteger (L, 1);
    return 4;
}

// dofile of a script's own files: from those carried in the app.
int r_dofile (lua_State* L)
{
    const String path = String::fromUTF8 (luaL_checkstring (L, 1));
    const String name = path.fromFirstOccurrenceOf ("<embedded>/", false, false);
    for (const auto& f : embedded::files())
        if (name.isNotEmpty() && String (f.name) == name)
        {
            const int base = lua_gettop (L);
            if (luaL_loadbuffer (L, f.data, (size_t) f.size, ("@" + name.fromLastOccurrenceOf ("/", false, false)).toRawUTF8()) != LUA_OK) return lua_error (L);
            lua_call (L, 0, LUA_MULTRET);
            return lua_gettop (L) - base;
        }
    lua_pushvalue (L, lua_upvalueindex (2));   // the real dofile
    lua_pushvalue (L, 1);
    lua_call (L, 1, LUA_MULTRET);
    return lua_gettop (L) - 1;
}
} // namespace

//==============================================================================
ReaperScript::ReaperScript (Controller& ctl, const String& p, Id f) : c (ctl), focus (f), path (p) {}

ReaperScript::~ReaperScript()
{
    stopTimer();
    if (! L) return;
    for (int ref : atExit)
    {
        lua_rawgeti (L, LUA_REGISTRYINDEX, ref);
        if (lua_pcall (L, 0, 0, 0) != LUA_OK) lua_pop (L, 1);
    }
    lua_close (L);
}

void ReaperScript::fail (const String& e)
{
    error = e;
    deferred.clear();
    stopTimer();
    c.say (path.fromLastOccurrenceOf ("/", false, false) + " stopped: " + e, true);
}

bool ReaperScript::start()
{
    const embedded::File* file = nullptr;
    for (const auto& f : embedded::files()) if (String (f.name) == path) file = &f;
    if (! file) { fail ("it isn't in Kalibre."); return false; }

    L = luaL_newstate();
    luaL_openlibs (L);
    // dofile: the script's own files first.
    lua_pushlightuserdata (L, this);
    lua_getglobal (L, "dofile");
    lua_pushcclosure (L, r_dofile, 2);
    lua_setglobal (L, "dofile");

    const std::pair<const char*, lua_CFunction> functions[] {
        { "GetMasterTrack", r_GetMasterTrack }, { "CountTracks", r_CountTracks }, { "GetTrack", r_GetTrack },
        { "GetTrackName", r_GetTrackName }, { "GetMediaTrackInfo_Value", r_GetMediaTrackInfo_Value },
        { "CountSelectedTracks", r_CountSelectedTracks }, { "GetSelectedTrack", r_GetSelectedTrack },
        { "CountSelectedMediaItems", r_CountSelectedMediaItems }, { "GetSelectedMediaItem", r_GetSelectedMediaItem },
        { "GetActiveTake", r_GetActiveTake }, { "TakeIsMIDI", r_TakeIsMIDI }, { "GetMediaItem_Track", r_GetMediaItem_Track },
        { "GetMediaItemTake_Source", r_GetMediaItemTake_Source }, { "GetMediaSourceLength", r_GetMediaSourceLength },
        { "GetMediaItemInfo_Value", r_GetMediaItemInfo_Value }, { "GetMediaItemTakeInfo_Value", r_GetMediaItemTakeInfo_Value },
        { "GetTakeName", r_GetTakeName }, { "GetSetMediaItemTakeInfo_String", r_GetSetMediaItemTakeInfo_String },
        { "MIDI_CountEvts", r_MIDI_CountEvts }, { "MIDI_GetNote", r_MIDI_GetNote },
        { "MIDI_GetProjQNFromPPQPos", r_MIDI_GetProjQNFromPPQPos },
        { "GetNumTakeMarkers", r_GetNumTakeMarkers }, { "GetTakeMarker", r_GetTakeMarker },
        { "CountProjectMarkers", r_CountProjectMarkers }, { "EnumProjectMarkers", r_EnumProjectMarkers },
        { "EnumProjectMarkers3", r_EnumProjectMarkers3 },
        { "TimeMap2_timeToQN", r_TimeMap2_timeToQN }, { "TimeMap2_QNToTime", r_TimeMap2_QNToTime },
        { "TimeMap_GetTimeSigAtTime", r_TimeMap_GetTimeSigAtTime }, { "Master_GetTempo", r_Master_GetTempo },
        { "TrackFX_GetCount", r_TrackFX_GetCount }, { "TrackFX_GetNumParams", r_TrackFX_GetNumParams },
        { "TrackFX_GetParamName", r_TrackFX_GetParamName }, { "TrackFX_GetParam", r_TrackFX_GetParam },
        { "TrackFX_SetParam", r_TrackFX_SetParam }, { "TrackFX_SetParamNormalized", r_TrackFX_SetParamNormalized },
        { "GetTouchedOrFocusedFX", r_GetTouchedOrFocusedFX }, { "GetFocusedFX", r_GetFocusedFX },
        { "gmem_attach", r_gmem_attach }, { "gmem_read", r_gmem_read }, { "gmem_write", r_gmem_write },
        { "defer", r_defer }, { "atexit", r_atexit }, { "time_precise", r_time_precise },
        { "MB", r_MB }, { "ShowMessageBox", r_MB }, { "ShowConsoleMsg", r_nothing },
        { "GetUserFileNameForRead", r_cancelled }, { "GetUserInputs", r_cancelled },
        { "MarkProjectDirty", r_MarkProjectDirty }, { "GetResourcePath", r_GetResourcePath },
        { "RecursiveCreateDirectory", r_RecursiveCreateDirectory }, { "get_action_context", r_get_action_context },
        { "Undo_BeginBlock", r_nothing }, { "Undo_EndBlock", r_nothing }, { "UpdateArrange", r_nothing },
        { "SetToggleCommandState", r_nothing }, { "RefreshToolbar", r_nothing }, { "RefreshToolbar2", r_nothing },
        { "set_action_options", r_nothing },
    };
    lua_newtable (L);
    for (auto& [name, fn] : functions)
    {
        lua_pushlightuserdata (L, this);
        lua_pushcclosure (L, fn, 1);
        lua_setfield (L, -2, name);
    }
    lua_setglobal (L, "reaper");

    const auto chunk = "@" + path.fromLastOccurrenceOf ("/", false, false);
    if (luaL_loadbuffer (L, file->data, (size_t) file->size, chunk.toRawUTF8()) != LUA_OK
        || lua_pcall (L, 0, 0, 0) != LUA_OK)
    {
        fail (String::fromUTF8 (lua_tostring (L, -1)));
        lua_pop (L, 1);
        return false;
    }
    if (waiting()) startTimerHz (30);
    return true;
}

void ReaperScript::step()
{
    if (! L) return;
    auto now = std::move (deferred);
    deferred.clear();
    for (int ref : now)
    {
        lua_rawgeti (L, LUA_REGISTRYINDEX, ref);
        luaL_unref (L, LUA_REGISTRYINDEX, ref);
        if (lua_pcall (L, 0, 0, 0) != LUA_OK)
        {
            fail (String::fromUTF8 (lua_tostring (L, -1)));
            lua_pop (L, 1);
            return;
        }
    }
    if (! waiting()) stopTimer();
    else if (! isTimerRunning()) startTimerHz (30);
}

//==============================================================================
void ReaperScripts::sync()
{
    // Finished runs go.
    runs.erase (std::remove_if (runs.begin(), runs.end(), [] (const auto& r) { return ! r->waiting(); }), runs.end());
    bool wanted = false;
    for (auto& t : c.project.tracks)
        for (auto& d : t.devices) wanted = wanted || d.plugin.identifier == "jsfx:midi-production-center";
    if (wanted && ! loader)
    {
        loader = std::make_unique<ReaperScript> (c, "scripts/midi-production-center/MPC - Loader (background).lua", 0);
        if (! loader->start()) loader.reset();
    }
    else if (! wanted) loader.reset();
}

bool ReaperScripts::loadChosenClips (Id device)
{
    const auto* d = c.project.device (device);
    if (! d) return false;
    String path;
    if (d->plugin.identifier == "jsfx:midular") path = "scripts/midular/midular_import.lua";
    else if (d->plugin.identifier == "jsfx:midi-production-center") path = "scripts/midi-production-center/MPC - Load selected MIDI item.lua";
    else return false;
    auto run = std::make_unique<ReaperScript> (c, path, device);
    const bool ok = run->start();
    if (run->waiting()) runs.push_back (std::move (run));
    return ok;
}

} // namespace kal
