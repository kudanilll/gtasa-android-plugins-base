#pragma once

#include <plugin-sdk/game_sa/plugin.h>
#include <plugin-sdk/renderware/RwTexture.h>

namespace TextureLoader {
// Render thread only, after RenderWare initialization. Caller owns the returned texture.
// Release it with RwTextureDestroy before RenderWare shuts down. Returns nullptr on failure.
RwTexture *LoadTextureFromFile(const char *filePath);
} // namespace TextureLoader
