#include "texture_loader.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_MAX_DIMENSIONS 4096
#include <mod/thirdparty/stb/stb_image.h>

#include <cstring>
#include <memory>
#include <mod/logger.h>
#include <plugin-sdk/game_sa/utils/TextureUtils.h>

namespace TextureLoader {
RwTexture *LoadTextureFromFile(const char *filePath) {
    if (!filePath || !*filePath || !RwImageCreate || !RwImageAllocatePixels || !RwImageDestroy ||
        !RwImageFindRasterFormat || !RwRasterCreate || !RwRasterSetFromImage || !RwRasterDestroy || !RwTextureCreate ||
        !RwTextureSetName)
        return nullptr;

    logger->Info("Texture: decode begin: %s", filePath);
    int width{}, height{}, channels{};
    if (!stbi_info(filePath, &width, &height, &channels) || width <= 0 || height <= 0 || width > STBI_MAX_DIMENSIONS ||
        height > STBI_MAX_DIMENSIONS) {
        logger->Info("Texture: missing or invalid image; using text button");
        return nullptr;
    }
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load(filePath, &width, &height, &channels, STBI_rgb_alpha), stbi_image_free);
    if (!pixels) {
        logger->Error("Texture: decode failed: %s", stbi_failure_reason());
        return nullptr;
    }
    logger->Info("Texture: RenderWare upload begin (%dx%d)", width, height);
    const auto freeImage = [](RwImage *image) { RwImageDestroy(image); };
    std::unique_ptr<RwImage, decltype(freeImage)> image(RwImageCreate(width, height, 32), freeImage);
    if (!image || !RwImageAllocatePixels(image.get()))
        return nullptr;

    for (int y = 0; y < height; ++y) {
        std::memcpy(image->cpPixels + y * image->stride, pixels.get() + y * width * 4, width * 4);
    }

    RwTexture *texture = TextureUtils::CreateFromImage(image.get(), "button");
    logger->Info("Texture: RenderWare upload %s", texture ? "complete" : "failed");
    return texture;
}
} // namespace TextureLoader
