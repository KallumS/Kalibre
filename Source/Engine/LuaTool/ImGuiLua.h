// ReaImGui's Lua API on Dear ImGui (ImGuiLua.cpp).
#pragma once
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <map>
#include <vector>

struct ImFont;

namespace kal::tools {

// A font asked for: its size in points, and 1 << 16 when bold.
inline int fontKey(int size, bool bold) { return size | (bold ? 1 << 16 : 0); }
inline int fontKeySize(int key) { return key & 0xffff; }
inline bool fontKeyBold(int key) { return (key >> 16) != 0; }

// What the script asked for that the device, not ImGui, acts on.
struct ImGuiLuaState {
    int configFlags = 0;
    std::vector<int> fontSizes;          // CreateFont + Attach, in order (fontKey)
    bool fontsChanged = false;
    std::map<int, ImFont *> fonts;       // by fontKey, once the atlas is built
    float preferredW = 0, preferredH = 0;
    float minW = 0, minH = 0;
    bool windowOpen = false;
};

// One script runs at a time on the message thread; the session in use sets
// its own ImGui context current and owns this.
extern ImGuiLuaState *g_imgui;

// Sets the global ImGui table.
void openImGui(lua_State *L);

} // namespace kal::tools
