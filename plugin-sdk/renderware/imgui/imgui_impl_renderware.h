#pragma once

#include <mod/thirdparty/imgui/imgui.h>

// User callback function type for drawing custom ImGui windows
typedef void (*ImGui_RenderCallback)();

// Initializes ImGui, RenderWare rendering backend, and hooks touch & render loop automatically.
// buttonPath: optional path to a custom PNG/JPG button icon. If NULL, automatically searches
// for button.png or button.jpg in the game files/data directory, falling back to a "MENU" button.
bool ImGui_ImplRenderWare_Init(ImGui_RenderCallback renderCallback = nullptr, const char *buttonPath = nullptr);

// Shuts down ImGui, cleans up textures, and restores original hooks
void ImGui_ImplRenderWare_Shutdown();

// Query or change menu window visibility
bool ImGui_ImplRenderWare_IsMenuOpen();
void ImGui_ImplRenderWare_SetMenuOpen(bool open);
void ImGui_ImplRenderWare_ToggleMenu();

// Floating toggle button configuration
void ImGui_ImplRenderWare_SetButtonVisible(bool visible);
void ImGui_ImplRenderWare_SetButtonSize(float width, float height);

// Low-level renderer APIs (for developers who manage their own render loop)
bool ImGui_ImplRenderWare_InitRenderer();
bool ImGui_ImplRenderWare_NewFrame(int width, int height, float deltaTime);
bool ImGui_ImplRenderWare_RenderDrawData(ImDrawData *drawData);
