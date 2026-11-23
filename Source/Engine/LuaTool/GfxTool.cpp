#include "GfxTool.h"
#include "ToolSession.h"
#include "Theme.h"

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include <cfloat>

namespace kal::tools
{
using namespace juce;

static GfxSession* self (lua_State* L) { return static_cast<GfxSession*> (lua_touserdata (L, lua_upvalueindex (1))); }

static int packed (const char* s)
{
    int v = 0;
    for (; *s; ++s) v = v * 256 + (unsigned char) *s;
    return v;
}

// ---- helpers ---------------------------------------------------------------

double GfxSession::getnum (const char* field, double fallback)
{
    lua_getglobal (L, "gfx");
    lua_getfield (L, -1, field);
    const double v = lua_isnumber (L, -1) ? lua_tonumber (L, -1) : fallback;
    lua_pop (L, 2);
    return v;
}

void GfxSession::setnum (const char* field, double v)
{
    lua_getglobal (L, "gfx");
    if (v == std::floor (v) && std::abs (v) < 1e15) lua_pushinteger (L, (lua_Integer) v);
    else lua_pushnumber (L, v);
    lua_setfield (L, -2, field);
    lua_pop (L, 1);
}

static Colour colourNow (GfxSession& s)
{
    auto c = [] (double v) { return (uint8) jlimit (0, 255, (int) std::lround (v * 255)); };
    return Colour (c (s.getnum ("r", 1)), c (s.getnum ("g", 1)), c (s.getnum ("b", 1))).withAlpha ((float) jlimit (0.0, 1.0, s.getnum ("a", 1)));
}

// The first drawing of a frame clears to gfx.clear as the script set it for
// this frame (REAPER clears after each gfx.update).
void GfxSession::clearIfNeeded()
{
    if (! needsClear || g == nullptr) return;
    needsClear = false;
    const double c = getnum ("clear", 0);
    if (c >= 0)
    {
        const auto v = (int64) c;
        g->fillAll (Colour ((uint8) (v & 255), (uint8) ((v >> 8) & 255), (uint8) ((v >> 16) & 255)));
    }
}

void GfxSession::refreshSize()
{
    if (! windowOpen) return;
    setnum ("w", image.isValid() ? image.getWidth() : preferredWidth);
    setnum ("h", image.isValid() ? image.getHeight() : preferredHeight);
    if (wantRetina) setnum ("ext_retina", scale);
}

static GfxSession::Font& fontAt (GfxSession& s, int slot)
{
    auto it = s.fonts.find (slot);
    if (it != s.fonts.end()) return it->second;
    auto& f = s.fonts[slot];
    f.font = Font (FontOptions().withPointHeight (13.0f * (float) s.scale));
    f.face = "System";
    return f;
}

static double lineHeight (GfxSession& s, int slot) { return std::ceil (fontAt (s, slot).font.getHeight()); }
static double measure (GfxSession& s, int slot, const String& text) { return GlyphArrangement::getStringWidth (fontAt (s, slot).font, text); }

// ---- gfx ---------------------------------------------------------------------

static int g_init (lua_State* L)
{
    auto& s = *self (L);
    const int w = (int) luaL_optinteger (L, 2, 0), h = (int) luaL_optinteger (L, 3, 0);
    if (! s.windowOpen)
    {
        s.wantRetina = s.getnum ("ext_retina", 0) > 0;
        if (w > 0) s.preferredWidth = w;
        if (h > 0) s.preferredHeight = h;
    }
    s.windowOpen = true;   // the window is the device's; a title is nowhere to show
    s.refreshSize();
    lua_pushinteger (L, 1);
    return 1;
}

static int g_set (lua_State* L)
{
    auto& s = *self (L);
    const double r = luaL_checknumber (L, 1);
    s.setnum ("r", r);
    s.setnum ("g", luaL_optnumber (L, 2, r));
    s.setnum ("b", luaL_optnumber (L, 3, r));
    s.setnum ("a", luaL_optnumber (L, 4, 1));
    return 0;
}

static int g_rect (lua_State* L)
{
    auto& s = *self (L);
    const double x = luaL_checknumber (L, 1), y = luaL_checknumber (L, 2);
    const double w = luaL_checknumber (L, 3), h = luaL_checknumber (L, 4);
    const bool filled = lua_isnoneornil (L, 5) || lua_tonumber (L, 5) != 0;
    if (w <= 0 || h <= 0 || s.g == nullptr) return 0;
    s.clearIfNeeded();
    s.g->setColour (colourNow (s));
    const Rectangle<float> r ((float) x, (float) y, (float) w, (float) h);
    if (filled) s.g->fillRect (r);
    else s.g->drawRect (r, 1.0f);   // a one-pixel frame inside it
    return 0;
}

static int g_triangle (lua_State* L)
{
    auto& s = *self (L);
    const int n = jmin (32, lua_gettop (L) / 2);
    if (n < 3 || s.g == nullptr) return 0;
    Path p;
    for (int i = 0; i < n; ++i)
    {
        const auto px = (float) luaL_checknumber (L, i * 2 + 1), py = (float) luaL_checknumber (L, i * 2 + 2);
        if (i == 0) p.startNewSubPath (px, py);
        else p.lineTo (px, py);
    }
    p.closeSubPath();
    s.clearIfNeeded();
    s.g->setColour (colourNow (s));
    s.g->fillPath (p);
    return 0;
}

// gfx.setfont(slot[, face, size, flags]): any face is the system's own (the
// script asks for San Francisco on a Mac, and keeps whatever it is told it got).
static int g_setfont (lua_State* L)
{
    auto& s = *self (L);
    const int slot = (int) luaL_checkinteger (L, 1);
    s.currentFont = slot;
    if (lua_isstring (L, 2))
    {
        const String face = String::fromUTF8 (lua_tostring (L, 2));
        const double size = jmax (1.0, luaL_optnumber (L, 3, 14));
        int flags = (int) luaL_optinteger (L, 4, 0);
        bool bold = false, italic = false;
        for (; flags != 0; flags >>= 8)
        {
            bold = bold || (flags & 255) == 'b';
            italic = italic || (flags & 255) == 'i';
        }
        auto& f = s.fonts[slot];
        auto options = FontOptions().withPointHeight ((float) size);
        if (bold || italic) options = options.withStyle (bold && italic ? "Bold Italic" : bold ? "Bold" : "Italic");
        f.font = Font (options);
        f.face = face;
    }
    s.setnum ("texth", lineHeight (s, slot));
    lua_pushinteger (L, 1);
    return 1;
}

static int g_getfont (lua_State* L)
{
    auto& s = *self (L);
    lua_pushinteger (L, s.currentFont);
    lua_pushstring (L, fontAt (s, s.currentFont).face.toRawUTF8());
    return 2;
}

static int g_measurestr (lua_State* L)
{
    auto& s = *self (L);
    const auto text = String::fromUTF8 (luaL_tolstring (L, 1, nullptr));
    lua_pushnumber (L, measure (s, s.currentFont, text));
    lua_pushnumber (L, lineHeight (s, s.currentFont));
    return 2;
}

// gfx.drawstr(str[, flags, right, bottom]): flags 1 centre across, 2 right,
// 4 centre down, 8 bottom, 256 don't clip to the box. The pen moves to the
// end of what was drawn.
static int g_drawstr (lua_State* L)
{
    auto& s = *self (L);
    const int flags = (int) luaL_optinteger (L, 2, 0);
    const bool box = ! lua_isnoneornil (L, 3) && ! lua_isnoneornil (L, 4);
    const double right = box ? luaL_checknumber (L, 3) : 0, bottom = box ? luaL_checknumber (L, 4) : 0;
    const auto text = String::fromUTF8 (luaL_tolstring (L, 1, nullptr));
    const double x0 = s.getnum ("x", 0), y0 = s.getnum ("y", 0);
    auto& f = fontAt (s, s.currentFont);
    const double w = measure (s, s.currentFont, text), h = lineHeight (s, s.currentFont);
    double x = x0, y = y0;
    if (box)
    {
        if (flags & 1) x = x0 + (right - x0 - w) / 2;
        else if (flags & 2) x = right - w;
        if (flags & 4) y = y0 + (bottom - y0 - h) / 2;
        else if (flags & 8) y = bottom - h;
    }
    if (s.g != nullptr)
    {
        s.clearIfNeeded();
        Graphics::ScopedSaveState keep (*s.g);
        if (box && ! (flags & 256))
            s.g->reduceClipRegion (Rectangle<double> (x0, y0, right - x0, bottom - y0).toNearestInt());
        s.g->setColour (colourNow (s));
        s.g->setFont (f.font);
        GlyphArrangement ga;
        ga.addLineOfText (f.font, text, (float) x, (float) y + f.font.getAscent());
        ga.draw (*s.g);
    }
    s.setnum ("x", x + w);
    return 0;
}

static int g_update (lua_State* L)
{
    auto& s = *self (L);
    s.clearIfNeeded();
    s.needsClear = true;
    return 0;
}

static int g_getchar (lua_State* L)
{
    auto& s = *self (L);
    if (! lua_isnoneornil (L, 1)) { lua_pushinteger (L, 7); return 1; }   // open, focused, visible
    if (s.keyRead >= s.keys.size())
    {
        s.keys.clear();
        s.keyRead = 0;
        lua_pushinteger (L, 0);
        return 1;
    }
    const auto k = s.keys[s.keyRead++];
    s.setnum ("mouse_cap", s.input.buttons | k.mods);   // the modifiers as they were when the key went down
    lua_pushinteger (L, k.code);
    return 1;
}

// gfx.showmenu: REAPER's menu string ("!" checked, "#" greyed, ">" opens a
// submenu, "<" ends one, "" a line), at gfx.x, gfx.y. Kalibre's own menu.
static int g_showmenu (lua_State* L)
{
    auto& s = *self (L);
    const auto items = StringArray::fromTokens (String::fromUTF8 (luaL_checkstring (L, 1)), "|", "");
    std::vector<PopupMenu> stack (1);
    std::vector<String> titles (1);
    int n = 0;
    for (auto field : items)
    {
        if (field.isEmpty()) { stack.back().addSeparator(); continue; }
        bool grey = false, check = false, sub = false, last = false;
        int i = 0;
        for (; i < field.length(); ++i)
        {
            const auto c = field[i];
            if (c == '#') grey = true;
            else if (c == '!') check = true;
            else if (c == '>') sub = true;
            else if (c == '<') last = true;
            else break;
        }
        const auto label = field.substring (i);
        if (sub) { stack.emplace_back(); titles.push_back (label); }
        else stack.back().addItem (++n, label, ! grey, check);
        if (last && stack.size() > 1)
        {
            auto m = stack.back();
            stack.pop_back();
            stack.back().addSubMenu (titles.back(), m);
            titles.pop_back();
        }
    }
    while (stack.size() > 1)
    {
        auto m = stack.back();
        stack.pop_back();
        stack.back().addSubMenu (titles.back(), m);
        titles.pop_back();
    }
    int pick = 0;
#if JUCE_MODAL_LOOPS_PERMITTED
    auto options = PopupMenu::Options();
    if (s.menuParent != nullptr)
    {
        const auto at = s.menuParent->localPointToGlobal ((Point<double> (s.getnum ("x", 0), s.getnum ("y", 0)) / s.scale).toFloat()).toInt();
        options = options.withTargetScreenArea (Rectangle<int> (at.x, at.y, 1, 1));
    }
    pick = stack.front().showMenu (options);
#endif
    s.input.buttons = 0;   // the mouse went up inside the menu; the window never heard it
    lua_pushinteger (L, pick);
    return 1;
}

static int g_dock (lua_State* L)
{
    auto& s = *self (L);
    lua_pushinteger (L, 0);
    lua_pushinteger (L, 0);
    lua_pushinteger (L, 0);
    lua_pushinteger (L, s.preferredWidth);
    lua_pushinteger (L, s.preferredHeight);
    return 5;
}

static int g_nothing (lua_State*) { return 0; }

// ---- reaper --------------------------------------------------------------------

static int r_defer (lua_State* L)
{
    auto& s = *self (L);
    luaL_checktype (L, 1, LUA_TFUNCTION);
    luaL_unref (L, LUA_REGISTRYINDEX, s.deferredRef);
    lua_pushvalue (L, 1);
    s.deferredRef = luaL_ref (L, LUA_REGISTRYINDEX);
    return 0;
}

static int r_atexit (lua_State* L)
{
    auto& s = *self (L);
    luaL_checktype (L, 1, LUA_TFUNCTION);
    luaL_unref (L, LUA_REGISTRYINDEX, s.atexitRef);
    lua_pushvalue (L, 1);
    s.atexitRef = luaL_ref (L, LUA_REGISTRYINDEX);
    return 0;
}

static int r_get_action_context (lua_State* L)
{
    const auto& spec = self (L)->processor.spec;
    lua_pushboolean (L, 0);
    lua_pushstring (L, (String ("<embedded>/tools/") + spec.id + "/" + spec.script).toRawUTF8());
    for (int i = 0; i < 5; ++i) lua_pushinteger (L, -1);
    lua_pushstring (L, "");
    return 8;
}

static String extKey (lua_State* L) { return String (luaL_checkstring (L, 1)) + "/" + luaL_checkstring (L, 2); }

static int r_getext (lua_State* L)
{
    const auto& ext = self (L)->processor.ext;
    const auto it = ext.find (extKey (L));
    lua_pushstring (L, it == ext.end() ? "" : it->second.toRawUTF8());
    return 1;
}

static int r_setext (lua_State* L)
{
    self (L)->processor.ext[extKey (L)] = String::fromUTF8 (luaL_checkstring (L, 3));
    return 0;
}

static int r_hasext (lua_State* L)
{
    const auto& ext = self (L)->processor.ext;
    const auto it = ext.find (extKey (L));
    lua_pushboolean (L, it != ext.end() && it->second.isNotEmpty());
    return 1;
}

// REAPER's message box, asked as Kalibre asks (inside the main window, 0002): never
// waiting for the click, which on Windows JUCE's own box did, so a script stopped there.
static int r_message (lua_State* L)
{
    auto* w = new AlertWindow (String::fromUTF8 (luaL_optstring (L, 2, self (L)->processor.spec.name)),
                               String::fromUTF8 (luaL_checkstring (L, 1)), MessageBoxIconType::NoIcon);
    w->addButton ("OK", 1, KeyPress (KeyPress::returnKey));
    ui::present (w, nullptr, true);
    lua_pushinteger (L, 1);
    return 1;
}

static int r_time_precise (lua_State* L) { lua_pushnumber (L, Time::getMillisecondCounterHiRes() / 1000.0); return 1; }

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

static int r_resource (lua_State* L)
{
    const auto d = File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Kalibre").getChildFile (self (L)->processor.spec.name);
    lua_pushstring (L, d.getFullPathName().toRawUTF8());
    return 1;
}

static int r_nothing (lua_State*) { return 0; }

// ---- running it ----------------------------------------------------------------

static int traceback (lua_State* L)
{
    luaL_traceback (L, L, luaL_tolstring (L, 1, nullptr), 1);
    return 1;
}

bool GfxSession::call (int nargs, int nresults)
{
    const int base = lua_gettop (L) - nargs;
    lua_pushcfunction (L, traceback);
    lua_insert (L, base);
    const int rc = lua_pcall (L, nargs, nresults, base);
    lua_remove (L, base);
    if (rc != LUA_OK)
    {
        failure = String::fromUTF8 (lua_tostring (L, -1) ? lua_tostring (L, -1) : "Unknown error.");
        lua_pop (L, 1);
        return false;
    }
    return true;
}

static void reg (lua_State* L, GfxSession* s, const char* name, lua_CFunction f)
{
    lua_pushlightuserdata (L, s);
    lua_pushcclosure (L, f, 1);
    lua_setfield (L, -2, name);
}

GfxSession::GfxSession (GfxProcessor& p) : processor (p)
{
    L = luaL_newstate();
    luaL_openlibs (L);

    lua_getglobal (L, "dofile");
    lua_pushcclosure (L, embeddedDofile, 1);
    lua_setglobal (L, "dofile");

    lua_newtable (L);
    const std::pair<const char*, lua_CFunction> gfxFns[] = {
        { "init", g_init }, { "set", g_set }, { "rect", g_rect }, { "triangle", g_triangle },
        { "setfont", g_setfont }, { "getfont", g_getfont }, { "measurestr", g_measurestr },
        { "drawstr", g_drawstr }, { "update", g_update }, { "getchar", g_getchar },
        { "showmenu", g_showmenu }, { "dock", g_dock }, { "quit", g_nothing } };
    for (auto& [name, fn] : gfxFns) reg (L, this, name, fn);
    for (auto name : { "r", "g", "b", "a" }) { lua_pushnumber (L, 1); lua_setfield (L, -2, name); }
    for (auto name : { "x", "y", "w", "h", "clear", "mouse_x", "mouse_y", "mouse_cap", "mouse_wheel", "ext_retina" })
    {
        lua_pushinteger (L, 0);
        lua_setfield (L, -2, name);
    }
    lua_pushinteger (L, 14);
    lua_setfield (L, -2, "texth");
    lua_setglobal (L, "gfx");

    lua_newtable (L);
    const std::pair<const char*, lua_CFunction> reaperFns[] = {
        { "defer", r_defer }, { "atexit", r_atexit }, { "get_action_context", r_get_action_context },
        { "GetExtState", r_getext }, { "SetExtState", r_setext }, { "HasExtState", r_hasext },
        { "ShowMessageBox", r_message }, { "MB", r_message }, { "ShowConsoleMsg", r_nothing },
        { "time_precise", r_time_precise }, { "GetOS", r_getos }, { "GetResourcePath", r_resource } };
    for (auto& [name, fn] : reaperFns) reg (L, this, name, fn);
    lua_setglobal (L, "reaper");

    const auto path = String ("<embedded>/tools/") + p.spec.id + "/" + p.spec.script;
    lua_pushcfunction (L, [] (lua_State* l) -> int
    {
        lua_getglobal (l, "dofile");
        lua_pushvalue (l, 1);
        lua_call (l, 1, 0);
        return 0;
    });
    lua_pushstring (L, path.toRawUTF8());
    inFrame = true;
    call (1, 0);   // runs the script to its first defer
    inFrame = false;
}

GfxSession::~GfxSession()
{
    if (L == nullptr) return;
    if (atexitRef != LUA_NOREF)
    {
        lua_rawgeti (L, LUA_REGISTRYINDEX, atexitRef);
        call (0, 0);
    }
    g.reset();
    lua_close (L);
}

void GfxSession::key (int code, int mods)
{
    if (keys.size() < 256) keys.push_back ({ code, mods });
}

void GfxSession::frame (int w, int h, double density, Component* menuOver)
{
    if (L == nullptr || inFrame || failure.isNotEmpty() || deferredRef == LUA_NOREF) return;
    w = jmax (1, w);
    h = jmax (1, h);
    if (! image.isValid() || image.getWidth() != w || image.getHeight() != h)
        image = Image (Image::ARGB, w, h, true);
    scale = density;
    menuParent = menuOver;
    g = std::make_unique<Graphics> (image);
    needsClear = true;

    refreshSize();
    setnum ("mouse_x", input.x);
    setnum ("mouse_y", input.y);
    setnum ("mouse_cap", input.buttons | input.mods);
    if (input.wheel != 0)
    {
        setnum ("mouse_wheel", getnum ("mouse_wheel", 0) + input.wheel);
        input.wheel = 0;
    }

    // Kept until the script defers another: the window is Kalibre's, so a
    // script that would close itself carries on.
    const int fn = deferredRef;
    deferredRef = LUA_NOREF;
    inFrame = true;
    lua_rawgeti (L, LUA_REGISTRYINDEX, fn);
    const bool ok = call (0, 0);
    inFrame = false;
    if (deferredRef == LUA_NOREF && ok) deferredRef = fn;
    else luaL_unref (L, LUA_REGISTRYINDEX, fn);
    g.reset();
}

String GfxSession::run (const String& lua)
{
    if (L == nullptr) return "no script";
    if (luaL_loadstring (L, lua.toRawUTF8()) != LUA_OK)
    {
        String e = String::fromUTF8 (lua_tostring (L, -1));
        lua_pop (L, 1);
        return e;
    }
    const auto before = failure;
    if (! call (0, 1)) { auto e = failure; failure = before; return e; }
    String result = String::fromUTF8 (luaL_tolstring (L, -1, nullptr));
    lua_pop (L, 2);
    return result;
}

// ---- the device ----------------------------------------------------------------

GfxProcessor::GfxProcessor (const LuaToolSpec& s)
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      spec (s)
{
    current = std::make_unique<GfxSession> (*this);
}

GfxProcessor::~GfxProcessor() { current.reset(); }

void GfxProcessor::getStateInformation (MemoryBlock& dest)
{
    ValueTree root ("KalibreTool");
    root.setProperty ("tool", spec.id, nullptr);
    root.setProperty ("editorWidth", editorWidth, nullptr);
    root.setProperty ("editorHeight", editorHeight, nullptr);
    ValueTree e ("ext");
    for (auto& kv : ext)
    {
        ValueTree item ("item");
        item.setProperty ("key", kv.first, nullptr);
        item.setProperty ("value", kv.second, nullptr);
        e.addChild (item, -1, nullptr);
    }
    root.addChild (e, -1, nullptr);
    MemoryOutputStream stream (dest, false);
    root.writeToStream (stream);
}

void GfxProcessor::setStateInformation (const void* data, int size)
{
    const auto root = ValueTree::readFromData (data, (size_t) size);
    if (! root.hasType ("KalibreTool")) return;
    editorWidth = root.getProperty ("editorWidth", 0);
    editorHeight = root.getProperty ("editorHeight", 0);
    std::map<String, String> keep;
    for (auto item : root.getChildWithName ("ext"))
        keep[item.getProperty ("key").toString()] = item.getProperty ("value").toString();
    // The script reads its settings when it starts, so it starts again.
    auto restart = [this, keep] {
        current.reset();
        ext = keep;
        current = std::make_unique<GfxSession> (*this);
    };
    if (MessageManager::getInstance()->isThisTheMessageThread()) restart();
    else MessageManager::callAsync (restart);
}

namespace
{
// The script's window: a picture it draws each frame, and the mouse and keys
// handed to it as gfx has them.
class GfxEditor : public AudioProcessorEditor, private Timer
{
public:
    explicit GfxEditor (GfxProcessor& p) : AudioProcessorEditor (p), proc (p)
    {
        auto* s = p.session();
        const int w = p.editorWidth > 0 ? p.editorWidth : (s ? s->preferredWidth : 760);
        const int h = p.editorHeight > 0 ? p.editorHeight : (s ? s->preferredHeight : 680);
        setWantsKeyboardFocus (true);
        setResizable (true, true);
        setResizeLimits (460, 360, 4096, 4096);
        setSize (w, h);
        startTimerHz (30);
        timerCallback();
    }

    void paint (Graphics& gr) override
    {
        gr.fillAll (theme::ground);
        if (auto* s = proc.session())
        {
            if (s->picture().isValid())
                gr.drawImage (s->picture(), getLocalBounds().toFloat(), RectanglePlacement::stretchToFit);
            if (s->error().isNotEmpty())
            {
                gr.setColour (theme::control);
                gr.setFont (FontOptions (14.0f));
                gr.drawFittedText (String (proc.spec.name) + " stopped:\n\n" + s->error(), getLocalBounds().reduced (16), Justification::topLeft, 40);
            }
        }
    }

    void resized() override
    {
        proc.editorWidth = getWidth();
        proc.editorHeight = getHeight();
    }

    void mouseMove (const MouseEvent& e) override { at (e); }
    void mouseEnter (const MouseEvent& e) override { at (e); }
    void mouseDrag (const MouseEvent& e) override { at (e); }
    void mouseExit (const MouseEvent&) override
    {
        if (auto* s = proc.session(); s && s->input.buttons == 0) s->input.x = s->input.y = -10000;
    }
    void mouseDown (const MouseEvent& e) override
    {
        grabKeyboardFocus();
        at (e);
        if (auto* s = proc.session())
        {
            const int b = button (e);
            s->input.buttons |= b;
            fresh |= b;
        }
    }
    void mouseUp (const MouseEvent& e) override
    {
        at (e);
        // A click can come and go between two frames; the button then stays
        // down until a frame has seen it.
        if (auto* s = proc.session())
        {
            for (int b : { 1, 2, 64 })
                if (s->input.buttons & b)
                {
                    if (fresh & b) letGo |= b;
                    else s->input.buttons &= ~b;
                }
        }
    }
    void mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w) override
    {
        at (e);
        if (auto* s = proc.session()) s->input.wheel += w.deltaY / 0.14 * 120.0;   // a notch of a mouse wheel is about 0.14
    }
    void modifierKeysChanged (const ModifierKeys& m) override
    {
        if (auto* s = proc.session()) s->input.mods = mods (m);
    }
    bool keyPressed (const KeyPress& k) override
    {
        auto* s = proc.session();
        if (s == nullptr) return false;
        const int m = mods (k.getModifiers());
        s->input.mods = m;
        const int c = k.getKeyCode();
        int code = 0;
        if (c == KeyPress::upKey) code = packed ("up");
        else if (c == KeyPress::downKey) code = packed ("down");
        else if (c == KeyPress::leftKey) code = packed ("left");
        else if (c == KeyPress::rightKey) code = packed ("rght");
        else if (c == KeyPress::pageUpKey) code = packed ("pgup");
        else if (c == KeyPress::pageDownKey) code = packed ("pgdn");
        else if (c == KeyPress::homeKey) code = packed ("home");
        else if (c == KeyPress::endKey) code = packed ("end");
        else if (c == KeyPress::insertKey) code = packed ("ins");
        else if (c == KeyPress::deleteKey) code = packed ("del");
        else if (c == KeyPress::backspaceKey) code = 8;
        else if (c == KeyPress::returnKey) code = 13;
        else if (c == KeyPress::tabKey) code = 9;
        else if (c == KeyPress::escapeKey) code = 27;
        else if (c >= KeyPress::F1Key && c <= KeyPress::F12Key) code = packed (("f" + String (c - KeyPress::F1Key + 1)).toRawUTF8());
        if (code != 0) { s->key (code, m); return true; }
        if (m & (4 | 32))
        {
            // Command or Control: letters arrive as 1..26, as REAPER gives Ctrl+A
            const auto l = (juce_wchar) CharacterFunctions::toLowerCase ((juce_wchar) c);
            if (l >= 'a' && l <= 'z') s->key ((int) (l - 'a' + 1), m);
            else s->key (c, m);
            return true;
        }
        const auto t = k.getTextCharacter();
        if (t >= 32 && t < 127) s->key ((int) t, m);
        else if (t >= 160) s->key (0x75000000 + (int) t, m);   // 'u' + code point
        else return false;
        return true;
    }

private:
    GfxProcessor& proc;
    int fresh = 0, letGo = 0;

    double density() const
    {
        double d = 1;
        if (auto* peer = getPeer()) d = peer->getPlatformScaleFactor();
        if (auto* disp = Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds())) d = jmax (d, disp->scale);
        return d;
    }

    void at (const MouseEvent& e)
    {
        if (auto* s = proc.session())
        {
            s->input.x = e.position.x * density();
            s->input.y = e.position.y * density();
            s->input.mods = mods (e.mods);
        }
    }

    static int button (const MouseEvent& e)
    {
        if (e.mods.isPopupMenu() || e.mods.isRightButtonDown()) return 2;
        if (e.mods.isMiddleButtonDown()) return 64;
        return 1;
    }

    // gfx's mouse_cap bits: 4 Command (Ctrl elsewhere), 8 Shift, 16 Option, 32 Control
    static int mods (const ModifierKeys& m)
    {
        int v = 0;
        if (m.isCommandDown()) v |= 4;
        if (m.isShiftDown()) v |= 8;
        if (m.isAltDown()) v |= 16;
#if JUCE_MAC
        if (m.isCtrlDown()) v |= 32;
#endif
        return v;
    }

    void timerCallback() override
    {
        auto* s = proc.session();
        if (s == nullptr) return;
        const double d = density();
        s->frame ((int) std::lround (getWidth() * d), (int) std::lround (getHeight() * d), d, this);
        s->input.buttons &= ~letGo;
        letGo = 0;
        fresh = 0;
        repaint();
    }
};
} // namespace

AudioProcessorEditor* GfxProcessor::createEditor() { return new GfxEditor (*this); }

} // namespace kal::tools
