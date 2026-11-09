#pragma once

#include <mod/thirdparty/imgui/imgui.h>

// Called every rendered frame between ImGui::NewFrame() and ImGui::Render().
// The callback owns all UI policy: which windows exist, and when they are shown.
typedef void (*ImGui_RenderCallback)();

// Creates this plugin's ImGui context and resolves RenderWare symbols. The first plugin to initialize hooks
// touch, 2D render, pause-menu render and RenderWare shutdown; later plugins share those hooks, and the plugin
// whose window is touched draws on top. Touches inside ImGui windows go to ImGui; all others reach the game.
bool ImGui_ImplRenderWare_Init(ImGui_RenderCallback renderCallback = nullptr);

// Destroys the ImGui context and its textures. Hooks stay installed and pass input through to the game.
void ImGui_ImplRenderWare_Shutdown();

// Low-level renderer APIs (for developers who manage their own ImGui context and frame loop)
bool ImGui_ImplRenderWare_InitRenderer();
bool ImGui_ImplRenderWare_RenderDrawData(ImDrawData *drawData);
