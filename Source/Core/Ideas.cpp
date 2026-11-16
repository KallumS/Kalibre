#include "Ideas.h"

#include "EmbeddedLua.h"
#include "ScaleModel.h"

#include <algorithm>
#include <cmath>
#include <cstring>

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace kal::ideas
{
namespace
{
const char* cacheKey = "kalibre.engines";

const embedded::File* findFile (const std::string& name)
{
    for (const auto& f : embedded::files())
        if (name == f.name) return &f;
    return nullptr;
}

// engine("good-idea/gi_idea.lua"): runs an embedded file once and hands back
// what it returned, as the scripts' own dofile does (Miderator's LuaEngine).
int luaEngine (lua_State* L)
{
    const char* name = luaL_checkstring (L, 1);
    lua_getfield (L, LUA_REGISTRYINDEX, cacheKey);
    lua_getfield (L, -1, name);
    if (! lua_isnil (L, -1)) return 1;
    lua_pop (L, 1);
    const auto* file = findFile (name);
    if (file == nullptr) return luaL_error (L, "no engine file called %s", name);
    const std::string chunk = std::string ("@") + name;
    if (luaL_loadbufferx (L, file->data, file->size, chunk.c_str(), "t") != LUA_OK) return lua_error (L);
    lua_call (L, 0, 1);
    if (lua_isnil (L, -1)) { lua_pop (L, 1); lua_pushboolean (L, 1); }
    lua_pushvalue (L, -1);
    lua_setfield (L, -3, name);
    return 1;
}

// What Kalibre asks of Good Idea: a fresh state with the kind, bars and key
// set (the rest left on Any, as Good Idea starts), one idea at a seed, and
// its first part's notes.
const char* bridge = R"LUA(
local T = engine("good-idea/gi_theory.lua")
local I = engine("good-idea/gi_idea.lua").init(T)
return function (kind, bars, num, den, seed, root, iv)
  local st = I.newState()
  st.kind = kind
  if kind == "Drums" then st.drumBars = bars else st.motifBars = bars end
  if root then
    st.root = root
    -- The scale with the same notes; else major or minor, by its third.
    local found, minor = nil, false
    for _, x in ipairs(iv) do if x == 3 then minor = true end end
    for i, s in ipairs(T.SCALES) do
      if not found and #s.iv == #iv then
        local same = true
        for k = 1, #iv do if s.iv[k] ~= iv[k] then same = false end end
        if same then found = i end
      end
    end
    st.scale = found or (minor and 2 or 1)
    st.secondVoice = "Off"
  end
  I.clampState(st)
  local idea = I.make(st, I.meter(num, den), seed)
  local part = idea.block.parts[1] or { notes = {} }
  local notes = {}
  for i, n in ipairs(part.notes) do notes[i] = { n.start, n.len, n.pitch, n.vel or 100 } end
  return notes, idea.block.beats or 0, idea.block.name or "", idea.summary or ""
end
)LUA";

struct State
{
    lua_State* L = nullptr;
    int make = LUA_NOREF;
    std::string error;

    State()
    {
        L = luaL_newstate();
        luaL_openlibs (L);
        lua_newtable (L);
        lua_setfield (L, LUA_REGISTRYINDEX, cacheKey);
        lua_pushcfunction (L, luaEngine);
        lua_setglobal (L, "engine");
        if (luaL_loadbufferx (L, bridge, std::strlen (bridge), "@kalibre-ideas", "t") != LUA_OK || lua_pcall (L, 0, 1, 0) != LUA_OK)
        {
            error = lua_tostring (L, -1) ? lua_tostring (L, -1) : "Good Idea's engine didn't load.";
            lua_pop (L, 1);
            return;
        }
        make = luaL_ref (L, LUA_REGISTRYINDEX);
    }
    ~State() { if (L) lua_close (L); }
};

State& state()
{
    static State s;
    return s;
}

Idea run (const char* kind, int bars, int num, int den, int seed, const ScaleChoice* key)
{
    Idea out;
    auto& s = state();
    if (! s.error.empty()) { out.error = s.error; return out; }
    lua_State* L = s.L;
    const int top = lua_gettop (L);
    lua_rawgeti (L, LUA_REGISTRYINDEX, s.make);
    lua_pushstring (L, kind);
    lua_pushinteger (L, bars);
    lua_pushinteger (L, num);
    lua_pushinteger (L, den);
    lua_pushinteger (L, std::clamp (seed, 1, maxSeed));
    if (key)
    {
        // ScaleView's roots are Good Idea's, in the same order.
        lua_pushinteger (L, std::clamp (key->root, 0, (int) scaleview::roots.size() - 1) + 1);
        lua_newtable (L);
        const auto& iv = scaleview::scales[(size_t) std::clamp (key->scale, 0, (int) scaleview::scales.size() - 1)].intervals;
        for (size_t i = 0; i < iv.size(); ++i) { lua_pushinteger (L, iv[i]); lua_rawseti (L, -2, (lua_Integer) i + 1); }
    }
    else { lua_pushnil (L); lua_pushnil (L); }
    if (lua_pcall (L, 7, 4, 0) != LUA_OK)
    {
        out.error = lua_tostring (L, -1) ? lua_tostring (L, -1) : "Good Idea's engine failed.";
        lua_settop (L, top);
        return out;
    }
    // notes, beats, name, summary
    const auto ticks = [] (double quarters) { return (Tick) std::llround (quarters * PPQ); };
    out.length = ticks (lua_tonumber (L, -3));
    out.name = lua_tostring (L, -2) ? lua_tostring (L, -2) : "";
    out.summary = lua_tostring (L, -1) ? lua_tostring (L, -1) : "";
    const int list = top + 1;
    const lua_Integer n = luaL_len (L, list);
    for (lua_Integer i = 1; i <= n; ++i)
    {
        lua_rawgeti (L, list, i);
        double f[4] {};
        for (int k = 0; k < 4; ++k) { lua_rawgeti (L, -1, k + 1); f[k] = lua_tonumber (L, -1); lua_pop (L, 1); }
        lua_pop (L, 1);
        Note note;
        note.start = ticks (f[0]);
        note.length = std::max<Tick> (1, ticks (f[1]));
        note.pitch = std::clamp ((int) f[2], 0, 127);
        note.velocity = std::clamp ((int) f[3], 1, 127);
        out.notes.push_back (note);
    }
    lua_settop (L, top);
    return out;
}
} // namespace

Idea drums (int bars, int num, int den, int seed) { return run ("Drums", std::clamp (bars, 1, 16), num, den, seed, nullptr); }

Idea motif (const ScaleChoice& key, int bars, int num, int den, int seed)
{
    return run ("Motif", std::clamp (bars, 1, 4), num, den, seed, &key);
}

} // namespace kal::ideas
