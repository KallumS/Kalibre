#include "LuaHost.h"

#include "Edit.h"
#include "Harmony.h"
#include "ScaleModel.h"

#include <chrono>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace kal::script
{

namespace
{
struct State
{
    Project* project = nullptr;
    Context context;
    std::string printed;
    std::chrono::steady_clock::time_point deadline;
    bool timedOut = false;
};

State& stateOf (lua_State* L)
{
    lua_getfield (L, LUA_REGISTRYINDEX, "kalibre.state");
    auto* s = static_cast<State*> (lua_touserdata (L, -1));
    lua_pop (L, 1);
    return *s;
}

Project& projectOf (lua_State* L) { return *stateOf (L).project; }

Id checkId (lua_State* L, int i) { return static_cast<Id> (luaL_checkinteger (L, i)); }

void setInt (lua_State* L, const char* k, lua_Integer v) { lua_pushinteger (L, v); lua_setfield (L, -2, k); }
void setNum (lua_State* L, const char* k, double v) { lua_pushnumber (L, v); lua_setfield (L, -2, k); }
void setStr (lua_State* L, const char* k, const std::string& v) { lua_pushstring (L, v.c_str()); lua_setfield (L, -2, k); }
void setBool (lua_State* L, const char* k, bool v) { lua_pushboolean (L, v); lua_setfield (L, -2, k); }

const char* kindName (TrackKind k)
{
    switch (k)
    {
        case TrackKind::Track:  return "track";
        case TrackKind::Folder: return "folder";
        case TrackKind::Return: return "return";
        case TrackKind::Master: return "master";
        case TrackKind::Summing: return "summing";
        case TrackKind::Cue:    return "cue";
    }
    return "track";
}

void pushTrack (lua_State* L, const Track& t)
{
    lua_newtable (L);
    setInt (L, "id", t.id);
    setStr (L, "name", t.name);
    setStr (L, "kind", kindName (t.kind));
    setInt (L, "parent", t.parent);
    setNum (L, "volume", t.volumeDb);
    setNum (L, "pan", t.pan);
    setBool (L, "mute", t.mute);
    setBool (L, "solo", t.solo);
    setNum (L, "delay", t.delayMs);
    setInt (L, "colour", t.colour);
    setInt (L, "version", t.activeVersion + 1);
    setInt (L, "versions", static_cast<lua_Integer> (t.versions.size()));
}

void pushNote (lua_State* L, const Note& n)
{
    lua_newtable (L);
    setInt (L, "id", n.id);
    setInt (L, "start", n.start);
    setInt (L, "length", n.length);
    setInt (L, "pitch", n.pitch);
    setInt (L, "velocity", n.velocity);
    setInt (L, "channel", n.channel + 1);
    setInt (L, "articulation", n.articulation + 1);
}

lua_Integer field (lua_State* L, int t, const char* k, lua_Integer def)
{
    lua_getfield (L, t, k);
    const auto v = lua_isnumber (L, -1) ? lua_tointeger (L, -1) : def;
    lua_pop (L, 1);
    return v;
}

double fieldNum (lua_State* L, int t, const char* k, double def)
{
    lua_getfield (L, t, k);
    const auto v = lua_isnumber (L, -1) ? lua_tonumber (L, -1) : def;
    lua_pop (L, 1);
    return v;
}

std::set<Id> idSet (lua_State* L, int i)
{
    std::set<Id> out;
    if (lua_isinteger (L, i)) { out.insert (checkId (L, i)); return out; }
    luaL_checktype (L, i, LUA_TTABLE);
    const auto n = luaL_len (L, i);
    for (lua_Integer k = 1; k <= n; ++k)
    {
        lua_geti (L, i, k);
        if (lua_istable (L, -1)) { lua_getfield (L, -1, "id"); out.insert (static_cast<Id> (lua_tointeger (L, -1))); lua_pop (L, 1); }
        else out.insert (static_cast<Id> (lua_tointeger (L, -1)));
        lua_pop (L, 1);
    }
    return out;
}

//==============================================================================
int l_tracks (lua_State* L)
{
    auto& p = projectOf (L);
    lua_newtable (L);
    lua_Integer i = 1;
    for (auto& t : p.tracks) { pushTrack (L, t); lua_seti (L, -2, i++); }
    return 1;
}

int l_track (lua_State* L)
{
    const auto* t = projectOf (L).track (checkId (L, 1));
    if (! t) { lua_pushnil (L); return 1; }
    pushTrack (L, *t);
    return 1;
}

int l_addTrack (lua_State* L)
{
    auto& p = projectOf (L);
    const std::string name = luaL_optstring (L, 1, "Track");
    const std::string kind = luaL_optstring (L, 2, "track");
    const Id parent = static_cast<Id> (luaL_optinteger (L, 3, 0));
    const auto k = kind == "folder" ? TrackKind::Folder : kind == "return" ? TrackKind::Return : TrackKind::Track;
    lua_pushinteger (L, edit::addTrack (p, k, name, 0, parent));
    return 1;
}

int l_setTrack (lua_State* L)
{
    auto* t = projectOf (L).track (checkId (L, 1));
    luaL_checktype (L, 2, LUA_TTABLE);
    if (! t) return luaL_error (L, "no track with that id");
    lua_getfield (L, 2, "name");
    if (lua_isstring (L, -1)) t->name = lua_tostring (L, -1);
    lua_pop (L, 1);
    t->volumeDb = fieldNum (L, 2, "volume", t->volumeDb);
    t->pan = std::clamp (fieldNum (L, 2, "pan", t->pan), -1.0, 1.0);
    t->delayMs = fieldNum (L, 2, "delay", t->delayMs);
    t->colour = static_cast<std::uint32_t> (field (L, 2, "colour", t->colour));
    lua_getfield (L, 2, "mute"); if (lua_isboolean (L, -1)) t->mute = lua_toboolean (L, -1); lua_pop (L, 1);
    lua_getfield (L, 2, "solo"); if (lua_isboolean (L, -1)) t->solo = lua_toboolean (L, -1); lua_pop (L, 1);
    return 0;
}

int l_removeTrack (lua_State* L) { edit::removeTrack (projectOf (L), checkId (L, 1)); return 0; }

int l_newVersion (lua_State* L)
{
    lua_pushinteger (L, edit::newVersion (projectOf (L), checkId (L, 1)) + 1);
    return 1;
}

int l_clips (lua_State* L)
{
    const auto* t = projectOf (L).track (checkId (L, 1));
    lua_newtable (L);
    if (! t) return 1;
    lua_Integer i = 1;
    for (auto& c : t->clips())
    {
        lua_newtable (L);
        setInt (L, "id", c.id);
        setStr (L, "name", c.name);
        setStr (L, "kind", c.isMidi() ? "midi" : "audio");
        setInt (L, "start", c.start);
        setInt (L, "length", c.length);
        setBool (L, "muted", c.muted);
        lua_seti (L, -2, i++);
    }
    return 1;
}

int l_addMidiClip (lua_State* L)
{
    lua_pushinteger (L, edit::addMidiClip (projectOf (L), checkId (L, 1), luaL_checkinteger (L, 2),
                                           luaL_optinteger (L, 3, PPQ * 4)));
    return 1;
}

int l_notes (lua_State* L)
{
    const auto* c = projectOf (L).clip (checkId (L, 1));
    lua_newtable (L);
    if (! c) return 1;
    lua_Integer i = 1;
    for (auto& n : c->midi.notes) { pushNote (L, n); lua_seti (L, -2, i++); }
    return 1;
}

int l_addNote (lua_State* L)
{
    luaL_checktype (L, 2, LUA_TTABLE);
    Note n;
    n.start = field (L, 2, "start", 0);
    n.length = field (L, 2, "length", PPQ);
    n.pitch = static_cast<int> (field (L, 2, "pitch", 60));
    n.velocity = static_cast<int> (field (L, 2, "velocity", 100));
    n.channel = std::clamp (static_cast<int> (field (L, 2, "channel", 1)) - 1, 0, 15);
    n.articulation = static_cast<int> (field (L, 2, "articulation", 0)) - 1;
    lua_pushinteger (L, edit::addNote (projectOf (L), checkId (L, 1), n));
    return 1;
}

int l_setNote (lua_State* L)
{
    auto* c = projectOf (L).clip (checkId (L, 1));
    const Id id = checkId (L, 2);
    luaL_checktype (L, 3, LUA_TTABLE);
    if (! c) return luaL_error (L, "no clip with that id");
    for (auto& n : c->midi.notes)
    {
        if (n.id != id) continue;
        n.start = std::max<Tick> (0, field (L, 3, "start", n.start));
        n.length = std::max<Tick> (1, field (L, 3, "length", n.length));
        n.pitch = std::clamp (static_cast<int> (field (L, 3, "pitch", n.pitch)), 0, 127);
        n.velocity = std::clamp (static_cast<int> (field (L, 3, "velocity", n.velocity)), 1, 127);
        n.channel = std::clamp (static_cast<int> (field (L, 3, "channel", n.channel + 1)) - 1, 0, 15);
        n.articulation = static_cast<int> (field (L, 3, "articulation", n.articulation + 1)) - 1;
    }
    edit::sortNotes (c->midi);
    return 0;
}

int l_removeNotes (lua_State* L) { edit::removeNotes (projectOf (L), checkId (L, 1), idSet (L, 2)); return 0; }

int l_quantise (lua_State* L)
{
    std::set<Id> ids;
    if (lua_gettop (L) >= 4 && ! lua_isnil (L, 4)) ids = idSet (L, 4);
    edit::quantise (projectOf (L), checkId (L, 1), ids, luaL_optinteger (L, 2, PPQ / 4), luaL_optnumber (L, 3, 1.0));
    return 0;
}

int l_transpose (lua_State* L)
{
    auto& p = projectOf (L);
    const Id clip = checkId (L, 1);
    std::set<Id> ids;
    if (lua_gettop (L) >= 3 && ! lua_isnil (L, 3)) ids = idSet (L, 3);
    else if (auto* c = p.clip (clip)) for (auto& n : c->midi.notes) ids.insert (n.id);
    edit::transpose (p, clip, ids, static_cast<int> (luaL_checkinteger (L, 2)));
    return 0;
}

int l_tempo (lua_State* L)
{
    lua_pushnumber (L, projectOf (L).tempo.bpmAt (luaL_optinteger (L, 1, 0)));
    return 1;
}

int l_setTempo (lua_State* L)
{
    projectOf (L).tempo.setTempoAt (luaL_checkinteger (L, 1), luaL_checknumber (L, 2), lua_toboolean (L, 3));
    return 0;
}

int l_setMeter (lua_State* L)
{
    projectOf (L).tempo.setMeterAt (static_cast<int> (luaL_checkinteger (L, 1)) - 1,
                                    static_cast<int> (luaL_checkinteger (L, 2)), static_cast<int> (luaL_checkinteger (L, 3)));
    return 0;
}

int l_barStart (lua_State* L)
{
    lua_pushinteger (L, projectOf (L).tempo.barStart (static_cast<int> (luaL_checkinteger (L, 1)) - 1));
    return 1;
}

int l_scale (lua_State* L)
{
    const auto& s = projectOf (L).scale;
    lua_newtable (L);
    setStr (L, "root", scaleview::roots[static_cast<size_t> (s.root)].name);
    setStr (L, "scale", scaleview::scales[static_cast<size_t> (s.scale)].name);
    setStr (L, "name", harmony::scaleName (s));
    return 1;
}

int l_setScale (lua_State* L)
{
    auto& s = projectOf (L).scale;
    const std::string root = luaL_checkstring (L, 1), scale = luaL_checkstring (L, 2);
    bool found = false;
    for (size_t i = 0; i < scaleview::roots.size(); ++i) if (root == scaleview::roots[i].name) { s.root = static_cast<int> (i); found = true; }
    if (! found) return luaL_error (L, "unknown root %s", root.c_str());
    found = false;
    for (size_t i = 0; i < scaleview::scales.size(); ++i) if (scale == scaleview::scales[i].name) { s.scale = static_cast<int> (i); found = true; }
    if (! found) return luaL_error (L, "unknown scale %s", scale.c_str());
    return 0;
}

int l_inScale (lua_State* L)
{
    lua_pushboolean (L, harmony::inScale (projectOf (L).scale, static_cast<int> (luaL_checkinteger (L, 1))));
    return 1;
}

int l_chordAt (lua_State* L)
{
    auto& p = projectOf (L);
    lua_pushstring (L, harmony::chordName (harmony::soundingAt (p, luaL_checkinteger (L, 1)), p.scale).c_str());
    return 1;
}

int l_selection (lua_State* L)
{
    auto& s = stateOf (L);
    lua_newtable (L);
    lua_newtable (L);
    lua_Integer i = 1;
    for (auto id : s.context.selectedTracks) { lua_pushinteger (L, id); lua_seti (L, -2, i++); }
    lua_setfield (L, -2, "tracks");
    setInt (L, "clip", s.context.selectedClip);
    lua_newtable (L);
    i = 1;
    for (auto id : s.context.selectedNotes) { lua_pushinteger (L, id); lua_seti (L, -2, i++); }
    lua_setfield (L, -2, "notes");
    setInt (L, "cursor", s.context.cursor);
    setInt (L, "playhead", s.context.playhead);
    return 1;
}

int l_select (lua_State* L)
{
    auto& s = stateOf (L);
    luaL_checktype (L, 1, LUA_TTABLE);
    lua_getfield (L, 1, "clip");
    if (lua_isinteger (L, -1)) s.context.selectedClip = static_cast<Id> (lua_tointeger (L, -1));
    lua_pop (L, 1);
    lua_getfield (L, 1, "notes");
    if (lua_istable (L, -1)) s.context.selectedNotes = idSet (L, lua_gettop (L));
    lua_pop (L, 1);
    return 0;
}

int l_message (lua_State* L)
{
    auto& s = stateOf (L);
    const std::string text = luaL_checkstring (L, 1);
    if (s.context.message) s.context.message (text);
    else s.printed += text + "\n";
    return 0;
}

int l_action (lua_State* L)
{
    auto& s = stateOf (L);
    const std::string name = luaL_checkstring (L, 1);
    lua_pushboolean (L, s.context.action ? s.context.action (name) : false);
    return 1;
}

int l_print (lua_State* L)
{
    auto& s = stateOf (L);
    const int n = lua_gettop (L);
    for (int i = 1; i <= n; ++i)
    {
        if (i > 1) s.printed += "\t";
        s.printed += luaL_tolstring (L, i, nullptr);
        lua_pop (L, 1);
    }
    s.printed += "\n";
    return 0;
}

void hook (lua_State* L, lua_Debug*)
{
    auto& s = stateOf (L);
    if (std::chrono::steady_clock::now() > s.deadline)
    {
        s.timedOut = true;
        luaL_error (L, "the script took too long and was stopped");
    }
}
} // namespace

Result run (Project& project, const std::string& code, const std::string& name, Context context, double timeLimit)
{
    Result result;
    const Project before = project;

    State state;
    state.project = &project;
    state.context = std::move (context);
    state.deadline = std::chrono::steady_clock::now()
                   + std::chrono::milliseconds (static_cast<long long> (timeLimit * 1000.0));

    lua_State* L = luaL_newstate();
    luaL_openlibs (L);
    lua_pushlightuserdata (L, &state);
    lua_setfield (L, LUA_REGISTRYINDEX, "kalibre.state");
    lua_sethook (L, hook, LUA_MASKCOUNT, 10000);

    static const luaL_Reg api[] = {
        { "tracks", l_tracks }, { "track", l_track }, { "addTrack", l_addTrack }, { "setTrack", l_setTrack },
        { "removeTrack", l_removeTrack }, { "newVersion", l_newVersion },
        { "clips", l_clips }, { "addMidiClip", l_addMidiClip },
        { "notes", l_notes }, { "addNote", l_addNote }, { "setNote", l_setNote }, { "removeNotes", l_removeNotes },
        { "quantise", l_quantise }, { "transpose", l_transpose },
        { "tempo", l_tempo }, { "setTempo", l_setTempo }, { "setMeter", l_setMeter }, { "barStart", l_barStart },
        { "scale", l_scale }, { "setScale", l_setScale }, { "inScale", l_inScale }, { "chordAt", l_chordAt },
        { "selection", l_selection }, { "select", l_select },
        { "message", l_message }, { "action", l_action },
        { nullptr, nullptr },
    };
    luaL_newlib (L, api);
    lua_pushinteger (L, PPQ);
    lua_setfield (L, -2, "ppq");
    lua_setglobal (L, "kalibre");
    lua_pushcfunction (L, l_print);
    lua_setglobal (L, "print");

    const int status = luaL_loadbuffer (L, code.data(), code.size(), ("@" + name).c_str());
    if (status == LUA_OK && lua_pcall (L, 0, 0, 0) == LUA_OK)
    {
        result.ok = true;
    }
    else
    {
        result.ok = false;
        result.error = lua_tostring (L, -1) ? lua_tostring (L, -1) : "the script failed";
        // A failed script leaves nothing half done.
        project = before;
    }
    lua_close (L);

    result.printed = state.printed;
    result.after = state.context;
    result.changed = result.ok && ! (project == before);
    return result;
}

} // namespace kal::script
