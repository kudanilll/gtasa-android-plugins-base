#pragma once

namespace Gui {
// ImGui render callback: launcher button and the example trainer window. Render thread only.
void Draw();

// Releases RenderWare textures owned by the UI. Must run before RenderWare shuts down.
void Shutdown();
} // namespace Gui
