#include "imgui_impl_renderware.h"
#include "imgui_impl_renderware_internal.h"

#include <mod/logger.h>
#include <plugin-sdk/game_sa/plugin.h>
#include <plugin-sdk/gta_base/Rect.h>
#include <plugin-sdk/renderware/RwImage.h>
#include <plugin-sdk/renderware/RwRaster.h>
#include <plugin-sdk/renderware/RwRender.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {
using DrawIndexedFn = RwBool (*)(i32, RwIm2DVertex *, i32, RwImVertexIndex *, i32);
using ScissorFn = void (*)(CRect &);
using DefinedState2dFn = void (*)();

struct Backend {
    ImGuiContext *context = nullptr;
    DrawIndexedFn drawIndexed = nullptr;
    ScissorFn setScissor = nullptr;
    DefinedState2dFn definedState2d = nullptr;
    void **engine = nullptr;
    void **renderQueue = nullptr;
    float *nearZ = nullptr;
    float *recipNearClip = nullptr;
    std::vector<RwIm2DVertex> vertices;
};
Backend backend;
ImGuiRenderWareDetail::TouchInput input;

bool Ready() {
    return backend.context == ImGui::GetCurrentContext() && backend.engine && *backend.engine && backend.renderQueue &&
           *backend.renderQueue;
}

void SetState(RwRenderState state, uintptr_t value) { RwRenderStateSet(state, reinterpret_cast<void *>(value)); }

void SetupRenderState(const ImDrawList *, const ImDrawCmd *) {
    SetState(rwRENDERSTATEZTESTENABLE, false);
    SetState(rwRENDERSTATEZWRITEENABLE, false);
    SetState(rwRENDERSTATEVERTEXALPHAENABLE, true);
    SetState(rwRENDERSTATESRCBLEND, rwBLENDSRCALPHA);
    SetState(rwRENDERSTATEDESTBLEND, rwBLENDINVSRCALPHA);
    SetState(rwRENDERSTATEFOGENABLE, false);
    SetState(rwRENDERSTATECULLMODE, rwCULLMODECULLNONE);
    SetState(rwRENDERSTATEALPHATESTFUNCTION, rwALPHATESTFUNCTIONGREATER);
    SetState(rwRENDERSTATEALPHATESTFUNCTIONREF, 0);
    SetState(rwRENDERSTATETEXTUREFILTER, rwFILTERLINEAR);
    SetState(rwRENDERSTATETEXTUREADDRESS, rwTEXTUREADDRESSCLAMP);
}

void SetSamplerLinear(const ImDrawList *, const ImDrawCmd *) { SetState(rwRENDERSTATETEXTUREFILTER, rwFILTERLINEAR); }

void SetSamplerNearest(const ImDrawList *, const ImDrawCmd *) { SetState(rwRENDERSTATETEXTUREFILTER, rwFILTERNEAREST); }

struct SavedRenderState {
    static constexpr RwRenderState states[] = {
        rwRENDERSTATEZTESTENABLE,   rwRENDERSTATEZWRITEENABLE,      rwRENDERSTATEVERTEXALPHAENABLE,
        rwRENDERSTATESRCBLEND,      rwRENDERSTATEDESTBLEND,         rwRENDERSTATEFOGENABLE,
        rwRENDERSTATECULLMODE,      rwRENDERSTATEALPHATESTFUNCTION, rwRENDERSTATEALPHATESTFUNCTIONREF,
        rwRENDERSTATETEXTUREFILTER, rwRENDERSTATETEXTUREADDRESS,    rwRENDERSTATETEXTURERASTER};
    uintptr_t values[std::size(states)]{};
    bool valid[std::size(states)]{};

    SavedRenderState() {
        for (size_t i = 0; i < std::size(states); ++i)
            valid[i] = RwRenderStateGet(states[i], &values[i]) != 0;
    }
    ~SavedRenderState() {
        for (size_t i = 0; i < std::size(states); ++i)
            if (valid[i])
                SetState(states[i], values[i]);
    }
};

void DestroyTexture(ImTextureData *texture) {
    if (auto *raster = static_cast<RwRaster *>(texture->BackendUserData))
        RwRasterDestroy(raster);
    texture->BackendUserData = nullptr;
    texture->SetTexID(ImTextureID_Invalid);
    texture->SetStatus(ImTextureStatus_Destroyed);
}

bool UpdateTexture(ImTextureData *texture) {
    if (texture->Status == ImTextureStatus_WantDestroy) {
        if (texture->UnusedFrames > 0)
            DestroyTexture(texture);
        return true;
    }
    if (texture->Status != ImTextureStatus_WantCreate && texture->Status != ImTextureStatus_WantUpdates)
        return true;
    if (texture->Format != ImTextureFormat_RGBA32)
        return false;

    const bool creating = texture->Status == ImTextureStatus_WantCreate;
    const auto freeImage = [](RwImage *image) { RwImageDestroy(image); };
    std::unique_ptr<RwImage, decltype(freeImage)> image(RwImageCreate(texture->Width, texture->Height, 32), freeImage);
    if (!image || !RwImageAllocatePixels(image.get()))
        return false;
    if (!ImGuiRenderWareDetail::CopyPixels(image->cpPixels, image->stride,
                                           static_cast<const unsigned char *>(texture->GetPixels()),
                                           texture->GetPitch(), texture->Width, texture->Height))
        return false;

    auto *raster = static_cast<RwRaster *>(texture->BackendUserData);
    if (creating) {
        RwInt32 width{}, height{}, depth{}, format{};
        if (!RwImageFindRasterFormat(image.get(), rwRASTERTYPETEXTURE, &width, &height, &depth, &format))
            return false;
        raster = RwRasterCreate(width, height, depth, format);
        if (!raster)
            return false;
    }
    // shortcut: atlas updates upload the whole CPU image; use raster subregion uploads if profiling warrants it.
    if (!raster || !RwRasterSetFromImage(raster, image.get())) {
        if (creating && raster)
            RwRasterDestroy(raster);
        return false;
    }
    texture->BackendUserData = raster;
    texture->SetTexID(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(raster)));
    texture->SetStatus(ImTextureStatus_OK);
    return true;
}
} // namespace

bool ImGui_ImplRenderWare_Init() {
    if (!ImGui::GetCurrentContext() || backend.context)
        return false;
    backend.drawIndexed = GetMainLibrarySymbol<DrawIndexedFn>(
        "_Z35RwIm2DRenderIndexedPrimitive_BUGFIX15RwPrimitiveTypeP14RwOpenGLVertexiPti");
    if (!backend.drawIndexed)
        backend.drawIndexed = GetMainLibrarySymbol<DrawIndexedFn>(
            "_Z28RwIm2DRenderIndexedPrimitive15RwPrimitiveTypeP14RwOpenGLVertexiPti");
    backend.setScissor = GetMainLibrarySymbol<ScissorFn>("_ZN7CWidget10SetScissorER5CRect");
    backend.definedState2d = GetMainLibrarySymbol<DefinedState2dFn>("_Z14DefinedState2dv");
    backend.engine = GetMainLibrarySymbol<void **>("RwEngineInstance");
    backend.renderQueue = GetMainLibrarySymbol<void **>("renderQueue");
    backend.nearZ = GetMainLibrarySymbol<float *>("_ZN9CSprite2d11NearScreenZE");
    backend.recipNearClip = GetMainLibrarySymbol<float *>("_ZN9CSprite2d13RecipNearClipE");
    if (!backend.drawIndexed || !backend.setScissor || !backend.engine || !backend.renderQueue || !backend.nearZ ||
        !backend.recipNearClip || !RwRenderStateSet || !RwRenderStateGet || !RwImageCreate || !RwImageDestroy ||
        !RwImageAllocatePixels || !RwImageFindRasterFormat || !RwRasterCreate || !RwRasterDestroy ||
        !RwRasterSetFromImage) {
        logger->Error("ImGui/RW: required RenderWare symbol missing; backend disabled");
        return false;
    }
    auto &io = ImGui::GetIO();
    if (io.BackendRendererUserData)
        return false;
    backend.context = ImGui::GetCurrentContext();
    io.BackendRendererUserData = &backend;
    io.BackendRendererName = "imgui_impl_renderware";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    auto &platform = ImGui::GetPlatformIO();
    platform.Renderer_TextureMaxWidth = platform.Renderer_TextureMaxHeight = 4096;
    platform.DrawCallback_ResetRenderState = SetupRenderState;
    platform.DrawCallback_SetSamplerLinear = SetSamplerLinear;
    platform.DrawCallback_SetSamplerNearest = SetSamplerNearest;
    logger->Info("ImGui/RW: backend ready (%s, %d-bit)", IMGUI_VERSION, static_cast<int>(sizeof(void *) * 8));
    return true;
}

bool ImGui_ImplRenderWare_NewFrame(int width, int height, float deltaTime) {
    if (!Ready() || width <= 0 || height <= 0)
        return false;
    auto &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(1, 1);
    io.DeltaTime = std::isfinite(deltaTime) && deltaTime > 0 ? std::min(deltaTime, 0.1f) : 1.0f / 60.0f;
    for (const auto &event : input.Drain()) {
        io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
        if (event.x >= 0 && event.y >= 0)
            io.AddMousePosEvent(static_cast<float>(event.x), static_cast<float>(event.y));
        if (event.type == 2)
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        else if (event.type == 1 || event.type == 4)
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    }
    return true;
}

bool ImGui_ImplRenderWare_RenderDrawData(ImDrawData *drawData) {
    static_assert(sizeof(ImDrawIdx) == sizeof(RwImVertexIndex), "RenderWare requires 16-bit indices");
    static_assert(sizeof(RwIm2DVertex) == 28, "GTA SA mobile vertex layout mismatch");
    if (!Ready() || !drawData || !drawData->Valid)
        return false;
    if (drawData->Textures)
        for (auto *texture : *drawData->Textures)
            if (!UpdateTexture(texture)) {
                logger->Error("ImGui/RW: texture upload failed; draw skipped");
                return false;
            }
    if (drawData->TotalVtxCount == 0)
        return true;
    const float width = drawData->DisplaySize.x * drawData->FramebufferScale.x;
    const float height = drawData->DisplaySize.y * drawData->FramebufferScale.y;
    if (width <= 0 || height <= 0 || !std::isfinite(width) || !std::isfinite(height))
        return false;

    const SavedRenderState saved;
    SetupRenderState(nullptr, nullptr);
    for (const auto *list : drawData->CmdLists) {
        backend.vertices.resize(list->VtxBuffer.Size);
        for (int i = 0; i < list->VtxBuffer.Size; ++i) {
            const auto &src = list->VtxBuffer[i];
            auto &dst = backend.vertices[i];
            dst.x = (src.pos.x - drawData->DisplayPos.x) * drawData->FramebufferScale.x;
            dst.y = (src.pos.y - drawData->DisplayPos.y) * drawData->FramebufferScale.y;
            dst.z = *backend.nearZ;
            dst.rhw = *backend.recipNearClip;
            dst.emissiveColor = src.col;
            dst.u = src.uv.x;
            dst.v = src.uv.y;
        }
        for (const auto &command : list->CmdBuffer) {
            if (command.UserCallback) {
                if (command.UserCallback == ImDrawCallback_ResetRenderState)
                    SetupRenderState(list, &command);
                else
                    command.UserCallback(list, &command);
                continue;
            }
            const float left =
                std::clamp((command.ClipRect.x - drawData->DisplayPos.x) * drawData->FramebufferScale.x, 0.0f, width);
            const float top =
                std::clamp((command.ClipRect.y - drawData->DisplayPos.y) * drawData->FramebufferScale.y, 0.0f, height);
            const float right =
                std::clamp((command.ClipRect.z - drawData->DisplayPos.x) * drawData->FramebufferScale.x, 0.0f, width);
            const float bottom =
                std::clamp((command.ClipRect.w - drawData->DisplayPos.y) * drawData->FramebufferScale.y, 0.0f, height);
            if (left >= right || top >= bottom || command.ElemCount == 0)
                continue;
            if (command.IdxOffset > static_cast<unsigned>(list->IdxBuffer.Size) ||
                command.ElemCount > static_cast<unsigned>(list->IdxBuffer.Size) - command.IdxOffset ||
                command.VtxOffset >= static_cast<unsigned>(list->VtxBuffer.Size))
                continue;
            // SA-MP/GTA SA mobile scissor format: x=left, y=bottom, z=right, w=top
            struct {
                float left, bottom, right, top;
            } clip{left, bottom, right, top};
            backend.setScissor(*(CRect *)(void *)&clip);
            SetState(rwRENDERSTATETEXTURERASTER, static_cast<uintptr_t>(command.GetTexID()));
            backend.drawIndexed(rwPRIMTYPETRILIST, backend.vertices.data() + command.VtxOffset,
                                list->VtxBuffer.Size - static_cast<int>(command.VtxOffset),
                                list->IdxBuffer.Data + command.IdxOffset, static_cast<int>(command.ElemCount));
            struct {
                float left, bottom, right, top;
            } resetScissor{0.0f, 0.0f, 0.0f, 0.0f};
            backend.setScissor(*(CRect *)(void *)&resetScissor);
        }
    }
    SetState(rwRENDERSTATETEXTURERASTER, 0);
    struct {
        float left, bottom, right, top;
    } resetScissor{0.0f, 0.0f, 0.0f, 0.0f};
    backend.setScissor(*(CRect *)(void *)&resetScissor);
    if (backend.definedState2d)
        backend.definedState2d();
    return true;
}

bool ImGui_ImplRenderWare_ProcessTouch(int type, int finger, int x, int y, bool capture) {
    return input.Process(type, finger, x, y, capture);
}

void ImGui_ImplRenderWare_SetInputEnabled(bool enabled) { input.SetEnabled(enabled); }

void ImGui_ImplRenderWare_Shutdown() {
    input.SetEnabled(false);
    if (!backend.context || backend.context != ImGui::GetCurrentContext())
        return;
    for (auto *texture : ImGui::GetPlatformIO().Textures)
        if (texture->BackendUserData && texture->RefCount == 1)
            DestroyTexture(texture);
    auto &io = ImGui::GetIO();
    io.BackendRendererUserData = nullptr;
    io.BackendRendererName = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    ImGui::GetPlatformIO().ClearRendererHandlers();
    backend = Backend{};
}
