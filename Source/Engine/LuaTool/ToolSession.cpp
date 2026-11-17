#include "ToolSession.h"
#include "ToolProcessor.h"
#include "EmbeddedLua.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <cfloat>

namespace kal::tools
{
using namespace juce;

// The scripts' files, carried inside Kalibre (Engines/tools/). A script asks
// for "<embedded>/tools/<tool>/x.lua"; a name not in the tool's own folder is
// looked for beside them (tools/x.lua: the place stand-ins, imgui.lua).
static const char* EMBEDDED = "<embedded>";

static const embedded::File* findFile (const String& name)
{
    for (const auto& f : embedded::files())
        if (name == f.name) return &f;
    return nullptr;
}

const embedded::File* resource (const String& path)
{
    if (! path.startsWith (String (EMBEDDED) + "/")) return nullptr;
    const auto inside = path.fromFirstOccurrenceOf ("/", false, false);   // tools/<tool>/x.lua
    if (auto* f = findFile (inside)) return f;
    return findFile ("tools/" + inside.fromLastOccurrenceOf ("/", false, false));
}

static ToolSession* self (lua_State* L) { return static_cast<ToolSession*> (lua_touserdata (L, lua_upvalueindex (1))); }

static void reg (lua_State* L, ToolSession* s, const char* name, lua_CFunction f)
{
    lua_pushlightuserdata (L, s);
    lua_pushcclosure (L, f, 1);
    lua_setfield (L, -2, name);
}

// ImGui's own checks become Lua errors while a script runs (Lua is built as
// C++, so the error unwinds cleanly), and are ignored otherwise.
static lua_State* running = nullptr;

} // namespace kal::tools

void kalToolImAssertFailed (const char* expr)
{
    if (kal::tools::running) luaL_error (kal::tools::running, "ImGui check failed: %s", expr);
    DBG ("ImGui check failed: " << expr);
}

namespace kal::tools
{

// ---- dofile, for the script's own files ------------------------------------------

int embeddedDofile (lua_State* L)
{
    const String path = String::fromUTF8 (luaL_checkstring (L, 1));
    const auto* f = resource (path);
    if (f == nullptr)
    {
        lua_pushvalue (L, lua_upvalueindex (1));   // the real dofile
        lua_pushvalue (L, 1);
        lua_call (L, 1, LUA_MULTRET);
        return lua_gettop (L) - 1;
    }
    const int base = lua_gettop (L);
    const auto chunk = "@" + path.fromLastOccurrenceOf ("/", false, false);
    if (luaL_loadbuffer (L, f->data, (size_t) f->size, chunk.toRawUTF8()) != LUA_OK) return lua_error (L);
    lua_call (L, 0, LUA_MULTRET);
    return lua_gettop (L) - base;
}

// ---- reaper ---------------------------------------------------------------------

static int r_defer (lua_State* L)
{
    luaL_checktype (L, 1, LUA_TFUNCTION);
    lua_pushvalue (L, 1);
    lua_setfield (L, LUA_REGISTRYINDEX, "kal_deferred");
    return 0;
}

static int r_atexit (lua_State* L)
{
    luaL_checktype (L, 1, LUA_TFUNCTION);
    lua_pushvalue (L, 1);
    lua_setfield (L, LUA_REGISTRYINDEX, "kal_atexit");
    return 0;
}

static int r_get_action_context (lua_State* L)
{
    const auto& spec = self (L)->processor.spec;
    lua_pushboolean (L, 0);
    lua_pushstring (L, (String (EMBEDDED) + "/tools/" + spec.id + "/" + spec.script).toRawUTF8());
    lua_pushinteger (L, 0);
    lua_pushinteger (L, 0);
    return 4;
}

static int r_builtin (lua_State* L) { lua_pushstring (L, (String (EMBEDDED) + "/tools").toRawUTF8()); return 1; }

static int r_getos (lua_State* L)
{
#if JUCE_MAC
    lua_pushstring (L, "macOS-arm64");
#elif JUCE_WINDOWS
    lua_pushstring (L, "Win64");
#else
    lua_pushstring (L, "Other");
#endif
    return 1;
}

static int r_nothing (lua_State*) { return 0; }
static int r_new_array (lua_State* L) { lua_settop (L, 1); return 1; }

static int r_getext (lua_State* L)
{
    const auto key = String (luaL_checkstring (L, 1)) + "/" + luaL_checkstring (L, 2);
    const auto ext = self (L)->processor.extState();
    const auto it = ext.find (key);
    lua_pushstring (L, it == ext.end() ? "" : it->second.toRawUTF8());
    return 1;
}

static int r_setext (lua_State* L)
{
    const auto key = String (luaL_checkstring (L, 1)) + "/" + luaL_checkstring (L, 2);
    self (L)->processor.setExt (key, String::fromUTF8 (luaL_checkstring (L, 3)));
    return 0;
}

static int r_mb (lua_State* L)
{
    AlertWindow::showMessageBoxAsync (MessageBoxIconType::InfoIcon,
                                      String::fromUTF8 (luaL_optstring (L, 2, self (L)->processor.spec.name)),
                                      String::fromUTF8 (luaL_checkstring (L, 1)));
    lua_pushinteger (L, 1);
    return 1;
}

static int r_time_precise (lua_State* L)
{
    lua_pushnumber (L, Time::getMillisecondCounterHiRes() / 1000.0);
    return 1;
}

// ---- host: the device itself ---------------------------------------------------------

static double field (lua_State* L, int t, const char* k, double d)
{
    lua_getfield (L, t, k);
    const double v = lua_isnumber (L, -1) ? lua_tonumber (L, -1) : d;
    lua_pop (L, 1);
    return v;
}

static std::vector<BlockNote> notesAt (lua_State* L, int t)
{
    std::vector<BlockNote> notes;
    if (! lua_istable (L, t)) return notes;
    const auto n = (int) lua_rawlen (L, t);
    notes.reserve ((size_t) n);
    for (int i = 1; i <= n; ++i)
    {
        lua_rawgeti (L, t, i);
        const int e = lua_gettop (L);
        if (lua_istable (L, e))
        {
            BlockNote nt;
            nt.start = field (L, e, "start", 0);
            nt.len = field (L, e, "len", 0.25);
            nt.pitch = jlimit (0, 127, (int) field (L, e, "pitch", 60));
            nt.vel = jlimit (1, 127, (int) field (L, e, "vel", 100));
            notes.push_back (nt);
        }
        lua_pop (L, 1);
    }
    return notes;
}

static int h_tempo (lua_State* L) { lua_pushnumber (L, self (L)->processor.tempo()); return 1; }
static int h_cursor (lua_State* L) { lua_pushnumber (L, self (L)->processor.cursor()); return 1; }
static int h_timeSig (lua_State* L)
{
    lua_pushinteger (L, self (L)->processor.sigNum());
    lua_pushinteger (L, self (L)->processor.sigDen());
    return 2;
}

static int h_setBlock (lua_State* L)
{
    luaL_checktype (L, 1, LUA_TTABLE);
    self (L)->processor.setBlock (notesAt (L, 1), luaL_optnumber (L, 2, 0));
    return 0;
}

// host.insert({ { name = "Violin I", notes = {...} }, ... }, beats, name)
static int h_insert (lua_State* L)
{
    luaL_checktype (L, 1, LUA_TTABLE);
    ToolBlock b;
    b.beats = luaL_optnumber (L, 2, 0);
    b.name = luaL_optstring (L, 3, "");
    const auto n = (int) lua_rawlen (L, 1);
    for (int i = 1; i <= n; ++i)
    {
        lua_rawgeti (L, 1, i);
        const int e = lua_gettop (L);
        if (lua_istable (L, e))
        {
            ToolBlock::Part part;
            lua_getfield (L, e, "name");
            if (lua_isstring (L, -1)) part.name = lua_tostring (L, -1);
            lua_pop (L, 1);
            lua_getfield (L, e, "notes");
            for (const auto& nt : notesAt (L, lua_gettop (L)))
                part.notes.push_back ({ nt.start, nt.len, nt.pitch, nt.vel });
            lua_pop (L, 1);
            b.parts.push_back (std::move (part));
        }
        lua_pop (L, 1);
    }
    auto* owner = self (L)->processor.owner;
    lua_pushboolean (L, owner != nullptr && insertToolBlock && insertToolBlock (*owner, b));
    return 1;
}

static int h_withSong (lua_State* L) { lua_pushboolean (L, self (L)->processor.withSong.load()); return 1; }
static int h_setWithSong (lua_State* L)
{
    self (L)->processor.withSong = lua_toboolean (L, 1) != 0;
    self (L)->processor.setExt ("kalibre/withSong", lua_toboolean (L, 1) ? "1" : "0");
    return 0;
}

static int h_exportDir (lua_State* L)
{
    const auto d = File::getSpecialLocation (File::userMusicDirectory).getChildFile (self (L)->processor.spec.name);
    lua_pushstring (L, d.getFullPathName().toRawUTF8());
    return 1;
}
static int h_makeDir (lua_State* L) { File (String::fromUTF8 (luaL_checkstring (L, 1))).createDirectory(); return 0; }
static int h_reveal (lua_State* L) { File (String::fromUTF8 (luaL_checkstring (L, 1))).revealToUser(); return 0; }
static int h_audition (lua_State* L) { self (L)->processor.audition (lua_toboolean (L, 1) != 0); return 0; }
static int h_auditioning (lua_State* L) { lua_pushboolean (L, self (L)->processor.auditioning()); return 1; }
static int h_setLoop (lua_State* L) { self (L)->processor.loop = lua_toboolean (L, 1) != 0; return 0; }
static int h_auditionPosition (lua_State* L)
{
    if (! self (L)->processor.auditioning()) return 0;
    lua_pushnumber (L, self (L)->processor.auditionFraction());
    return 1;
}

// ---- the session ---------------------------------------------------------------------

static int traceback (lua_State* L)
{
    luaL_traceback (L, L, luaL_tolstring (L, 1, nullptr), 1);
    return 1;
}

static bool call (lua_State* L, int nargs, int nresults, String& error)
{
    const int base = lua_gettop (L) - nargs;
    lua_pushcfunction (L, traceback);
    lua_insert (L, base);
    auto* before = running;
    running = L;
    const int rc = lua_pcall (L, nargs, nresults, base);
    running = before;
    lua_remove (L, base);
    if (rc != LUA_OK)
    {
        error = String::fromUTF8 (lua_tostring (L, -1));
        lua_pop (L, 1);
        return false;
    }
    return true;
}

void ToolSession::use()
{
    ImGui::SetCurrentContext (context);
    g_imgui = &state;
}

ToolSession::ToolSession (ToolProcessor& p) : processor (p)
{
    context = ImGui::CreateContext();
    use();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigErrorRecoveryEnableAssert = false;
    io.DisplaySize = ImVec2 (800, 600);
    io.DeltaTime = 1.0f / 30;
    // JUCE smooths the shapes' edges itself; ImGui's own smoothing would draw
    // a fringe of extra triangles.
    auto& style = ImGui::GetStyle();
    style.AntiAliasedLines = style.AntiAliasedLinesUseTex = style.AntiAliasedFill = false;

    L = luaL_newstate();
    luaL_openlibs (L);
    openImGui (L);

    lua_getglobal (L, "dofile");
    lua_pushcclosure (L, embeddedDofile, 1);
    lua_setglobal (L, "dofile");

    lua_newtable (L);
    reg (L, this, "defer", r_defer);
    reg (L, this, "atexit", r_atexit);
    reg (L, this, "get_action_context", r_get_action_context);
    reg (L, this, "ImGui_GetBuiltinPath", r_builtin);
    reg (L, this, "GetOS", r_getos);
    reg (L, this, "GetExtState", r_getext);
    reg (L, this, "SetExtState", r_setext);
    reg (L, this, "MB", r_mb);
    reg (L, this, "ShowMessageBox", r_mb);
    reg (L, this, "time_precise", r_time_precise);
    reg (L, this, "new_array", r_new_array);
    for (auto name : { "SetToggleCommandState", "RefreshToolbar2", "set_action_options", "ShowConsoleMsg" })
        reg (L, this, name, r_nothing);
    lua_setglobal (L, "reaper");

    lua_newtable (L);
    reg (L, this, "tempo", h_tempo);
    reg (L, this, "cursor", h_cursor);
    reg (L, this, "timeSig", h_timeSig);
    reg (L, this, "setBlock", h_setBlock);
    reg (L, this, "insert", h_insert);
    reg (L, this, "withSong", h_withSong);
    reg (L, this, "setWithSong", h_setWithSong);
    reg (L, this, "exportDir", h_exportDir);
    reg (L, this, "makeDir", h_makeDir);
    reg (L, this, "reveal", h_reveal);
    reg (L, this, "audition", h_audition);
    reg (L, this, "auditioning", h_auditioning);
    reg (L, this, "setLoop", h_setLoop);
    reg (L, this, "auditionPosition", h_auditionPosition);
    lua_setglobal (L, "host");

    const auto path = String (EMBEDDED) + "/tools/" + p.spec.id + "/" + p.spec.script;
    const auto* f = resource (path);
    if (f == nullptr) { failure = "The window script is missing from Kalibre."; return; }
    if (luaL_loadbuffer (L, f->data, (size_t) f->size, ("@" + String (p.spec.script)).toRawUTF8()) != LUA_OK)
    {
        failure = String::fromUTF8 (lua_tostring (L, -1));
        lua_pop (L, 1);
        return;
    }
    if (! call (L, 0, 0, failure)) return;

    // One frame straight away, unseen, so the script builds its block and
    // hands it to the device even if the window is never opened.
    frame (state.preferredW > 0 ? state.preferredW : 800, state.preferredH > 0 ? state.preferredH : 600, 1);
}

ToolSession::~ToolSession()
{
    if (L != nullptr)
    {
        use();
        lua_getfield (L, LUA_REGISTRYINDEX, "kal_atexit");
        if (lua_isfunction (L, -1)) { String e; call (L, 0, 0, e); }
        else lua_pop (L, 1);
        lua_close (L);
    }
    ImGui::DestroyContext (context);
    g_imgui = nullptr;
}

// Each size the script asked for, in the system's own face, rasterised at the
// screen's density so text is sharp on Retina.
void ToolSession::buildFonts (float scale)
{
    if (! state.fontsChanged && fontScale == scale && fontAtlas.isValid()) return;
    state.fontsChanged = false;
    fontScale = scale;

    auto& io = ImGui::GetIO();
    io.Fonts->Clear();
    state.fonts.clear();

    static const ImWchar ranges[] = {
        0x0020, 0x00FF, 0x0100, 0x017F,
        0x2010, 0x2027, 0x2030, 0x203A,   // dashes, quotes, ellipsis
        0x2190, 0x21FF,                   // arrows
        0x2212, 0x2212, 0x2318, 0x2318, 0x2325, 0x2325,
        0x266D, 0x266F,                   // flat, natural, sharp
        0 };
    const StringArray candidates { "/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/Helvetica.ttc",
                                   "/Library/Fonts/Arial Unicode.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                                   "C:\\Windows\\Fonts\\segoeui.ttf" };
    String face;
    for (auto& c : candidates)
        if (File (c).existsAsFile()) { face = c; break; }

    auto sizes = state.fontSizes;
    if (sizes.empty()) sizes.push_back (13);
    for (int s : sizes)
    {
        ImFontConfig cfg;
        cfg.RasterizerDensity = scale;
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        ImFont* font = nullptr;
        if (face.isNotEmpty()) font = io.Fonts->AddFontFromFileTTF (face.toRawUTF8(), (float) s, &cfg, ranges);
        if (font == nullptr)
        {
            cfg.SizePixels = (float) s;
            font = io.Fonts->AddFontDefault (&cfg);
        }
        state.fonts[s] = font;
    }
    io.Fonts->Build();

    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsAlpha8 (&pixels, &w, &h);
    fontAtlas = Image (Image::SingleChannel, w, h, false);
    Image::BitmapData bd (fontAtlas, Image::BitmapData::writeOnly);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) *bd.getPixelPointer (x, y) = pixels[y * w + x];
    io.Fonts->SetTexID ((ImTextureID) 1);
}

void ToolSession::frame (float width, float height, float scale)
{
    if (L == nullptr) return;
    use();
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2 (jmax (1.0f, width), jmax (1.0f, height));
    io.DisplayFramebufferScale = ImVec2 (scale, scale);
    io.DeltaTime = 1.0f / 30;
    buildFonts (scale);

    ImGui::NewFrame();
    if (failure.isEmpty())
    {
        ImGuiErrorRecoveryState recover;
        ImGui::ErrorRecoveryStoreState (&recover);
        lua_getfield (L, LUA_REGISTRYINDEX, "kal_deferred");
        const int fn = lua_gettop (L);
        if (lua_isfunction (L, fn))
        {
            // Kept until the script defers another: the window is Kalibre's,
            // so a script that closes itself (Esc) carries on.
            lua_pushvalue (L, fn);
            lua_pushnil (L);
            lua_setfield (L, LUA_REGISTRYINDEX, "kal_deferred");
            if (! call (L, 0, 0, failure)) ImGui::ErrorRecoveryTryToRecoverState (&recover);
            lua_getfield (L, LUA_REGISTRYINDEX, "kal_deferred");
            const bool again = lua_isfunction (L, -1);
            lua_pop (L, 1);
            if (! again) lua_setfield (L, LUA_REGISTRYINDEX, "kal_deferred");
            else lua_pop (L, 1);
        }
        else lua_pop (L, 1);
    }
    if (failure.isNotEmpty())
    {
        // What went wrong, where the window would be, so it can be reported.
        ImGui::SetNextWindowPos (ImVec2 (0, 0));
        ImGui::SetNextWindowSize (io.DisplaySize);
        ImGui::Begin ("##error", nullptr, ImGuiWindowFlags_NoDecoration);
        ImGui::TextWrapped ("%s stopped:\n\n%s", processor.spec.name, failure.toRawUTF8());
        ImGui::End();
    }
    ImGui::Render();
}

String ToolSession::run (const String& lua)
{
    if (L == nullptr) return "no script";
    use();
    if (luaL_loadstring (L, lua.toRawUTF8()) != LUA_OK)
    {
        String e = String::fromUTF8 (lua_tostring (L, -1));
        lua_pop (L, 1);
        return e;
    }
    String e;
    if (! call (L, 0, 1, e)) return e;
    String result = String::fromUTF8 (luaL_tolstring (L, -1, nullptr));
    lua_pop (L, 2);
    return result;
}

ImDrawData* ToolSession::drawData() const
{
    ImGui::SetCurrentContext (context);
    return ImGui::GetDrawData();
}

void ToolSession::mouseMove (float x, float y) { use(); ImGui::GetIO().AddMousePosEvent (x, y); }
void ToolSession::mouseButton (int b, bool down) { use(); ImGui::GetIO().AddMouseButtonEvent (b, down); }
void ToolSession::mouseWheel (float dx, float dy) { use(); ImGui::GetIO().AddMouseWheelEvent (dx, dy); }
void ToolSession::modifiers (const ModifierKeys& m)
{
    use();
    auto& io = ImGui::GetIO();
#if JUCE_MAC
    io.AddKeyEvent (ImGuiMod_Super, m.isCommandDown());
    io.AddKeyEvent (ImGuiMod_Ctrl, m.isCtrlDown());
#else
    io.AddKeyEvent (ImGuiMod_Ctrl, m.isCtrlDown());
#endif
    io.AddKeyEvent (ImGuiMod_Shift, m.isShiftDown());
    io.AddKeyEvent (ImGuiMod_Alt, m.isAltDown());
}
void ToolSession::key (int k, bool down) { use(); ImGui::GetIO().AddKeyEvent ((ImGuiKey) k, down); }
void ToolSession::character (juce_wchar c) { use(); ImGui::GetIO().AddInputCharacter ((unsigned int) c); }
void ToolSession::focus (bool has) { use(); ImGui::GetIO().AddFocusEvent (has); }
bool ToolSession::wantsKeyboard() const
{
    ImGui::SetCurrentContext (context);
    return ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace kal::tools
