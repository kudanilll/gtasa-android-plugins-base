#pragma once

#include <mod/thirdparty/imgui/imgui.h>

// One ImGui context per plugin. Call these on the game's 2D/render thread.
bool ImGui_ImplRenderWare_Init();
bool ImGui_ImplRenderWare_NewFrame(int width, int height, float deltaTime);
bool ImGui_ImplRenderWare_RenderDrawData(ImDrawData *drawData);
void ImGui_ImplRenderWare_Shutdown();

// May be called from the input thread. True means this entire touch belongs to the UI.
// capture is considered only on press; releases belonging to the game always pass through.
// x/y must use NewFrame's render coordinates; see main.cpp for window-to-render mapping.
bool ImGui_ImplRenderWare_ProcessTouch(int type, int finger, int x, int y, bool capture);
void ImGui_ImplRenderWare_SetInputEnabled(bool enabled);

// ImTextureID stores RwRaster*, not RwTexture* or a GLuint.
