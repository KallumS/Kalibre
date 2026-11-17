// Dear ImGui's configuration for the Lua tools (IMGUI_USER_CONFIG, decision 0068).
#pragma once
// A failed ImGui check while a script runs becomes a Lua error its window
// shows, instead of stopping Kalibre (ToolSession.cpp).
void kalToolImAssertFailed (const char* expr);
#define IM_ASSERT(_EXPR) do { if (! (_EXPR)) kalToolImAssertFailed (#_EXPR); } while (0)
#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
#define IMGUI_DISABLE_DEMO_WINDOWS
#define IMGUI_DISABLE_DEBUG_TOOLS
