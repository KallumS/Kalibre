#include "Ideas.h"

#include "EmbeddedLua.h"
#include "ScaleModel.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// Lua is built as C++ (decision 0068).
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

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

// What Kalibre asks of Midi Suggester (as Miderator's adapter does): the notes
// read as a melody (chords wanted) or as chords (a melody wanted), its first
// suggestion at a seed, as notes in beats.
const char* suggestBridge = R"LUA(
local T = engine("midi-suggester/ms_theory.lua")
local R = engine("midi-suggester/ms_read.lua")
local H = engine("midi-suggester/ms_harmony.lua")
local Mel = engine("midi-suggester/ms_melody.lua")
local function pack(list)
  local out = {}
  for i, n in ipairs(list or {}) do out[i] = { n.start, n.len, n.pitch, n.vel or 100 } end
  return out
end
return function (want, notes, beats, barBeats, seed)
  local list = {}
  for i, n in ipairs(notes) do list[i] = { start = n[1], len = n[2], pitch = n[3], vel = n[4] } end
  -- Chords wanted: the notes are a melody; a melody wanted: they're chords.
  local r = R.analyse(list, barBeats, T, want == "chords" and "Melody" or "Chords")
  if not r then return nil, nil, "Midi Suggester found nothing to read." end
  r.beats = math.max(r.beats, beats)
  local key = T.key(r.keys[1].root, r.keys[1].scale)
  if want == "chords" then
    local sugs = H.suggest(r.line, T, { key = key, beats = r.beats, barBeats = barBeats, rhythm = 2,
                                        colour = 1 + seed % 2, variation = seed - 1 })
    local s = sugs[1]
    if not s then return nil, nil, "Midi Suggester had no chords for it." end
    local bass = {}
    for _, c in ipairs(s.chords) do
      bass[#bass + 1] = { start = c.start, len = c.len, pitch = H.BASS_LOW + (c.chord.root - H.BASS_LOW) % 12, vel = H.VELOCITY or 100 }
    end
    return pack(H.voice(s, r.line, false)), pack(bass), table.concat(s.symbols, "  ")
  end
  local sugs = Mel.suggest(r.chords, T, { key = key, beats = r.beats, barBeats = barBeats, density = 1 + seed % 3,
                                          register = 2, variation = seed - 1 })
  if not sugs[1] then return nil, nil, "Midi Suggester had no melody for them." end
  return pack(sugs[1].notes), {}, ""
end
)LUA";

struct State
{
    lua_State* L = nullptr;
    int make = LUA_NOREF, suggest = LUA_NOREF;
    std::string error, suggestError;

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
        if (luaL_loadbufferx (L, suggestBridge, std::strlen (suggestBridge), "@kalibre-suggest", "t") != LUA_OK || lua_pcall (L, 0, 1, 0) != LUA_OK)
        {
            suggestError = lua_tostring (L, -1) ? lua_tostring (L, -1) : "Midi Suggester's engine didn't load.";
            lua_pop (L, 1);
            return;
        }
        suggest = luaL_ref (L, LUA_REGISTRYINDEX);
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

// Notes in beats, to Lua and back.
void pushNotes (lua_State* L, const std::vector<Note>& notes)
{
    lua_createtable (L, (int) notes.size(), 0);
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        lua_createtable (L, 4, 0);
        const double f[4] { (double) n.start / PPQ, (double) n.length / PPQ, (double) n.pitch, (double) n.velocity };
        for (int k = 0; k < 4; ++k) { lua_pushnumber (L, f[k]); lua_rawseti (L, -2, k + 1); }
        lua_rawseti (L, -2, (lua_Integer) i + 1);
    }
}

std::vector<Note> readNotes (lua_State* L, int index)
{
    std::vector<Note> out;
    if (! lua_istable (L, index)) return out;
    const lua_Integer n = luaL_len (L, index);
    for (lua_Integer i = 1; i <= n; ++i)
    {
        lua_rawgeti (L, index, i);
        double f[4] {};
        for (int k = 0; k < 4; ++k) { lua_rawgeti (L, -1, k + 1); f[k] = lua_tonumber (L, -1); lua_pop (L, 1); }
        lua_pop (L, 1);
        Note note;
        note.start = (Tick) std::llround (f[0] * PPQ);
        note.length = std::max<Tick> (1, (Tick) std::llround (f[1] * PPQ));
        note.pitch = std::clamp ((int) f[2], 0, 127);
        note.velocity = std::clamp ((int) (f[3] > 0 ? f[3] : 100), 1, 127);
        out.push_back (note);
    }
    return out;
}

// Midi Suggester: "chords" for a melody, or "melody" over chords.
bool suggestFor (const char* want, const std::vector<Note>& notes, Tick length, int num, int den, int seed,
                 std::vector<Note>& first, std::vector<Note>& second, std::string& names, std::string& error)
{
    auto& s = state();
    if (! s.error.empty() || ! s.suggestError.empty()) { error = s.error.empty() ? s.suggestError : s.error; return false; }
    lua_State* L = s.L;
    const int top = lua_gettop (L);
    lua_rawgeti (L, LUA_REGISTRYINDEX, s.suggest);
    lua_pushstring (L, want);
    pushNotes (L, notes);
    lua_pushnumber (L, (double) length / PPQ);
    lua_pushnumber (L, num * 4.0 / std::max (1, den));
    lua_pushinteger (L, std::clamp (seed, 1, maxSeed));
    if (lua_pcall (L, 5, 3, 0) != LUA_OK)
    {
        error = lua_tostring (L, -1) ? lua_tostring (L, -1) : "Midi Suggester's engine failed.";
        lua_settop (L, top);
        return false;
    }
    if (lua_isnil (L, top + 1))
    {
        error = lua_tostring (L, top + 3) ? lua_tostring (L, top + 3) : "Midi Suggester had nothing to suggest.";
        lua_settop (L, top);
        return false;
    }
    first = readNotes (L, top + 1);
    second = readNotes (L, top + 2);
    names = lua_tostring (L, top + 3) ? lua_tostring (L, top + 3) : "";
    lua_settop (L, top);
    return true;
}
} // namespace

Harmony chordsFor (const std::vector<Note>& melody, Tick length, int num, int den, int seed)
{
    Harmony h;
    if (melody.empty()) { h.error = "A melody is needed to suggest chords for."; return h; }
    suggestFor ("chords", melody, length, num, den, seed, h.chords, h.bass, h.names, h.error);
    return h;
}

Idea melodyOver (const std::vector<Note>& chords, Tick length, int num, int den, int seed)
{
    Idea out;
    std::vector<Note> none;
    std::string names;
    if (chords.empty()) { out.error = "Chords are needed to suggest a melody over."; return out; }
    if (suggestFor ("melody", chords, length, num, den, seed, out.notes, none, names, out.error)) out.length = length;
    return out;
}

Idea drums (int bars, int num, int den, int seed) { return run ("Drums", std::clamp (bars, 1, 16), num, den, seed, nullptr); }

Idea motif (const ScaleChoice& key, int bars, int num, int den, int seed)
{
    return run ("Motif", std::clamp (bars, 1, 4), num, den, seed, &key);
}

} // namespace kal::ideas
