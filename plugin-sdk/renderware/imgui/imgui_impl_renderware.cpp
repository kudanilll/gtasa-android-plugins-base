#include "imgui_impl_renderware.h"
#include "imgui_shared.h"

#include <mod/amlmod.h>
#include <mod/interface.h>
#include <mod/logger.h>
#include <plugin-sdk/game_sa/Events.h>
#include <plugin-sdk/game_sa/base/Timer.h>
#include <plugin-sdk/game_sa/engine/RsGlobal.h>
#include <plugin-sdk/game_sa/plugin.h>
#include <plugin-sdk/gta_base/Rect.h>
#include <plugin-sdk/renderware/RwImage.h>
#include <plugin-sdk/renderware/RwRaster.h>
#include <plugin-sdk/renderware/RwRender.h>

#include <mod/thirdparty/imgui/imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace {
using DrawIndexedFn = RwBool (*)(i32, RwIm2DVertex *, i32, RwImVertexIndex *, i32);
using ScissorFn = void (*)(CRect &);
using DefinedState2dFn = void (*)();

// Client: this plugin's own ImGui context. Every plugin built from this template is a client.
bool g_bInitialized = false;
ImGuiContext *g_pImGuiContext = nullptr;
ImGui_RenderCallback g_UserRenderCallback = nullptr;
const ImGuiShared::API *g_pSharedAPI = nullptr;
ImGuiShared::Handle g_hClient = 0;

// Provider: used only by the first plugin to initialize; it owns the hooks and cross-plugin focus.
ImGuiShared::Coordinator g_Coordinator;

// Game pointers
DrawIndexedFn g_pfnDrawIndexed = nullptr;
ScissorFn g_pfnSetScissor = nullptr;
DefinedState2dFn g_pfnDefinedState2d = nullptr;
void **g_ppEngine = nullptr;
void **g_ppRenderQueue = nullptr;
float *g_pfNearZ = nullptr;
float *g_pfRecipNearClip = nullptr;

// Hooks (provider only)
void (*orig_AND_TouchEvent)(int, int, int, int) = nullptr;
void (*orig_MobileMenuRender)(void *) = nullptr;
void (*orig_ShutdownRenderWare)() = nullptr;

uint32_t g_LastRenderTime = 0;
uint32_t g_LastRenderFrame = 0xFFFFFFFF;
std::vector<RwIm2DVertex> g_VertexBuffer;

bool CopyPixels(unsigned char *dst, int dstPitch, const unsigned char *src, int srcPitch, int width, int height) {
    if (!dst || !src || width <= 0 || height <= 0)
        return false;
    const int rowBytes = width * 4;
    if (dstPitch < rowBytes || srcPitch < rowBytes)
        return false;
    for (int y = 0; y < height; ++y)
        std::memcpy(dst + static_cast<size_t>(y) * dstPitch, src + static_cast<size_t>(y) * srcPitch, rowBytes);
    return true;
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
    if (!CopyPixels(image->cpPixels, image->stride, static_cast<const unsigned char *>(texture->GetPixels()),
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

// Called by the provider once per frame for every plugin, back to front, with the touches this plugin owns.
void ClientFrame(void *, const ImGuiShared::Touch *events, uint32_t count, int32_t width, int32_t height,
                 float deltaTime) {
    if (!g_pImGuiContext)
        return;

    ImGui::SetCurrentContext(g_pImGuiContext);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = deltaTime;

    for (uint32_t i = 0; i < count; ++i) {
        const ImGuiShared::Touch &ev = events[i];
        io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
        io.AddMousePosEvent(ev.x, ev.y);
        if (ev.type == 2 /* TOUCH_PUSH */)
            io.AddMouseButtonEvent(0, true);
        else if (ev.type == 1 /* TOUCH_POP */ || ev.type == 4 /* TOUCH_CANCEL */)
            io.AddMouseButtonEvent(0, false);
    }

    const float uiScale = std::clamp(static_cast<float>(height) / 720.0f, 0.8f, 2.0f);
    ImGui::GetStyle().FontSizeBase = 22.0f * uiScale;
    ImGui::GetStyle().FramePadding = ImVec2(10.0f * uiScale, 6.0f * uiScale);

    ImGui::NewFrame();

    if (g_UserRenderCallback)
        g_UserRenderCallback();
    ImGui::EndFrame();

    // Only touches inside these rectangles reach ImGui; everything else (joystick, camera, gaps between
    // windows) stays with the game. Child windows are clipped by their parent, so only top-level windows count.
    // Collected after EndFrame: until then ImGui's invisible fallback "Debug" window (400x400 at 60,60) is still
    // active and would swallow taps on the radar and the top-left of the pause map.
    std::vector<ImGuiShared::Rect> rects;
    for (const ImGuiWindow *win : g_pImGuiContext->Windows) {
        if (win->Active && !win->Hidden && !(win->Flags & (ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_ChildWindow)))
            rects.push_back({win->Pos.x, win->Pos.y, win->Pos.x + win->Size.x, win->Pos.y + win->Size.y});
    }
    g_pSharedAPI->setRects(g_pSharedAPI->state, g_hClient, rects.data(), static_cast<uint32_t>(rects.size()));

    ImGui::Render();
    ImGui_ImplRenderWare_RenderDrawData(ImGui::GetDrawData());
}

void ClientReset(void *) { ImGui_ImplRenderWare_Shutdown(); }

void Hooked_AND_TouchEvent(int type, int finger, int x, int y) {
    // shortcut: touch coordinates are taken as render pixels (true on tested devices); pass the view size as the
    // last two arguments if a device renders at a different resolution than its screen.
    if (!g_Coordinator.ProcessTouch(type, finger, static_cast<float>(x), static_cast<float>(y), 0, 0) &&
        orig_AND_TouchEvent)
        orig_AND_TouchEvent(type, finger, x, y);
}

void RenderFrame() {
    if (!g_ppEngine || !*g_ppEngine || !g_ppRenderQueue || !*g_ppRenderQueue)
        return;

    RsGlobalType *pScreen = &RsGlobal;
    if (!pScreen || pScreen->maximumWidth <= 0 || pScreen->maximumHeight <= 0)
        return;

    const int width = pScreen->maximumWidth;
    const int height = pScreen->maximumHeight;

    // Render2dStuff and MobileMenu::Render may both run in one frame. The pause-mode clock keeps ticking
    // while the game is paused (the gameplay clock freezes), so the pause menu still gets fresh UI frames.
    const uint32_t frameTimeMS = CTimer::m_snTimeInMillisecondsPauseMode;
    if (g_LastRenderFrame == frameTimeMS && frameTimeMS != 0)
        return;
    g_LastRenderFrame = frameTimeMS;

    // Calculate delta time
    uint32_t currentClockMS = CTimer::GetTimeMS();
    float deltaTime = 1.0f / 60.0f;
    if (g_LastRenderTime > 0 && currentClockMS >= g_LastRenderTime) {
        deltaTime = static_cast<float>(currentClockMS - g_LastRenderTime) / 1000.0f;
        if (deltaTime <= 0.0f || deltaTime > 0.1f)
            deltaTime = 1.0f / 60.0f;
    }
    g_LastRenderTime = currentClockMS;

    g_Coordinator.Frame(width, height, deltaTime);
}

void Hooked_MobileMenuRender(void *menu) {
    if (orig_MobileMenuRender)
        orig_MobileMenuRender(menu);
    RenderFrame();
}

void Hooked_ShutdownRenderWare() {
    g_Coordinator.Reset(); // Every plugin frees its ImGui textures while RenderWare is still alive.
    if (orig_ShutdownRenderWare)
        orig_ShutdownRenderWare();
}

void InstallProviderHooks() {
    void *pTouchSym = (void *)aml->GetSym(GetMainLibrary(), "_Z14AND_TouchEventiiii");
#ifndef AML64
    if (!pTouchSym)
        pTouchSym = reinterpret_cast<void *>(GetMainLibraryAddress() + 0x2697C0);
#endif
    if (pTouchSym) {
        aml->Hook(pTouchSym, reinterpret_cast<void *>(&Hooked_AND_TouchEvent),
                  reinterpret_cast<void **>(&orig_AND_TouchEvent));
    }

    // Hook render loop (2D pass and pause menu)
    Events::drawingEvent.after += RenderFrame;

    void *pMobileMenuRender = reinterpret_cast<void *>(aml->GetSym(GetMainLibrary(), "_ZN10MobileMenu6RenderEv"));
    if (pMobileMenuRender) {
        aml->Hook(pMobileMenuRender, reinterpret_cast<void *>(&Hooked_MobileMenuRender),
                  reinterpret_cast<void **>(&orig_MobileMenuRender));
    }

    void *pShutdown = reinterpret_cast<void *>(aml->GetSym(GetMainLibrary(), "_ZN5CGame18ShutdownRenderWareEv"));
    if (pShutdown) {
        aml->Hook(pShutdown, reinterpret_cast<void *>(&Hooked_ShutdownRenderWare),
                  reinterpret_cast<void **>(&orig_ShutdownRenderWare));
    }
}

// C ABI shims: other plugins call these through the published API, so only POD crosses the boundary.
ImGuiShared::Handle SharedAdd(void *state, const ImGuiShared::Client *client) {
    return client ? static_cast<ImGuiShared::Coordinator *>(state)->Add(*client) : 0;
}

void SharedRemove(void *state, ImGuiShared::Handle id) { static_cast<ImGuiShared::Coordinator *>(state)->Remove(id); }

void SharedSetRects(void *state, ImGuiShared::Handle id, const ImGuiShared::Rect *rects, uint32_t count) {
    static_cast<ImGuiShared::Coordinator *>(state)->SetRects(id, rects, count);
}

// The first plugin to initialize publishes its coordinator through AML and installs the hooks; later plugins
// join it, so the touched plugin's windows draw on top of the others. AML cannot unhook or unregister an
// interface, so the provider plugin must stay loaded for the whole session.
const ImGuiShared::API *AcquireSharedAPI() {
    static const ImGuiShared::API *const api = [] {
        using namespace ImGuiShared;
        const auto *existing = static_cast<const API *>(GetInterface(interfaceName));
        if (existing && existing->version == version && existing->bytes >= sizeof(API))
            return existing;

        static API local{version, sizeof(API), &g_Coordinator, SharedAdd, SharedRemove, SharedSetRects};
        if (existing)
            logger->Error("ImGui/RW: shared ImGui interface v%u is incompatible; windows will not share focus",
                          existing->version);
        else
            RegisterInterface(interfaceName, &local);
        InstallProviderHooks();
        return static_cast<const API *>(&local);
    }();
    return api;
}
} // namespace

bool ImGui_ImplRenderWare_InitRenderer() {
    g_pfnDrawIndexed = GetMainLibrarySymbol<DrawIndexedFn>(
        "_Z35RwIm2DRenderIndexedPrimitive_BUGFIX15RwPrimitiveTypeP14RwOpenGLVertexiPti");
    if (!g_pfnDrawIndexed)
        g_pfnDrawIndexed = GetMainLibrarySymbol<DrawIndexedFn>(
            "_Z28RwIm2DRenderIndexedPrimitive15RwPrimitiveTypeP14RwOpenGLVertexiPti");

    g_pfnSetScissor = GetMainLibrarySymbol<ScissorFn>("_ZN7CWidget10SetScissorER5CRect");
    g_pfnDefinedState2d = GetMainLibrarySymbol<DefinedState2dFn>("_Z14DefinedState2dv");
    g_ppEngine = GetMainLibrarySymbol<void **>("RwEngineInstance");
    g_ppRenderQueue = GetMainLibrarySymbol<void **>("renderQueue");
    g_pfNearZ = GetMainLibrarySymbol<float *>("_ZN9CSprite2d11NearScreenZE");
    g_pfRecipNearClip = GetMainLibrarySymbol<float *>("_ZN9CSprite2d13RecipNearClipE");

    if (!g_pfnDrawIndexed || !g_pfnSetScissor || !g_ppEngine || !g_ppRenderQueue || !g_pfNearZ || !g_pfRecipNearClip ||
        !RwRenderStateSet || !RwRenderStateGet || !RwImageCreate || !RwImageDestroy || !RwImageAllocatePixels ||
        !RwImageFindRasterFormat || !RwRasterCreate || !RwRasterDestroy || !RwRasterSetFromImage) {
        logger->Error("ImGui/RW: required RenderWare symbols missing; renderer disabled");
        return false;
    }

    auto &io = ImGui::GetIO();
    io.BackendRendererName = "imgui_impl_renderware";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;

    auto &platform = ImGui::GetPlatformIO();
    platform.Renderer_TextureMaxWidth = platform.Renderer_TextureMaxHeight = 4096;
    platform.DrawCallback_ResetRenderState = SetupRenderState;
    platform.DrawCallback_SetSamplerLinear = SetSamplerLinear;
    platform.DrawCallback_SetSamplerNearest = SetSamplerNearest;
    return true;
}

bool ImGui_ImplRenderWare_Init(ImGui_RenderCallback renderCallback) {
    if (g_bInitialized)
        return true;

    logger->Info("ImGui/RW: initializing...");

    IMGUI_CHECKVERSION();
    g_pImGuiContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(g_pImGuiContext);

    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    ImGui::StyleColorsDark();

    g_UserRenderCallback = renderCallback;

    if (!ImGui_ImplRenderWare_InitRenderer()) {
        ImGui::DestroyContext(g_pImGuiContext);
        g_pImGuiContext = nullptr;
        return false;
    }

    g_pSharedAPI = AcquireSharedAPI();
    const ImGuiShared::Client client{nullptr, ClientFrame, ClientReset};
    g_hClient = g_pSharedAPI->add(g_pSharedAPI->state, &client);
    if (!g_hClient) {
        logger->Error("ImGui/RW: shared ImGui interface rejected this plugin");
        ImGui::DestroyContext(g_pImGuiContext);
        g_pImGuiContext = nullptr;
        return false;
    }

    g_bInitialized = true;
    logger->Info("ImGui/RW: initialized successfully");
    return true;
}

void ImGui_ImplRenderWare_Shutdown() {
    if (!g_bInitialized)
        return;

    if (g_hClient)
        g_pSharedAPI->remove(g_pSharedAPI->state, g_hClient);
    g_hClient = 0;

    if (g_pImGuiContext) {
        ImGui::SetCurrentContext(g_pImGuiContext);
        for (auto *texture : ImGui::GetPlatformIO().Textures) {
            if (texture->BackendUserData && texture->RefCount == 1)
                DestroyTexture(texture);
        }
        ImGui::GetPlatformIO().ClearRendererHandlers();
        ImGui::DestroyContext(g_pImGuiContext);
        g_pImGuiContext = nullptr;
    }

    g_bInitialized = false;
    logger->Info("ImGui/RW: shutdown completed");
}

bool ImGui_ImplRenderWare_RenderDrawData(ImDrawData *drawData) {
    static_assert(sizeof(ImDrawIdx) == sizeof(RwImVertexIndex), "RenderWare requires 16-bit indices");
    static_assert(sizeof(RwIm2DVertex) == 28, "GTA SA mobile vertex layout mismatch");
    if (!drawData || !drawData->Valid || !g_ppEngine || !*g_ppEngine || !g_ppRenderQueue || !*g_ppRenderQueue)
        return false;

    if (drawData->Textures) {
        for (auto *texture : *drawData->Textures) {
            if (!UpdateTexture(texture))
                return false;
        }
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
        g_VertexBuffer.resize(list->VtxBuffer.Size);
        for (int i = 0; i < list->VtxBuffer.Size; ++i) {
            const auto &src = list->VtxBuffer[i];
            auto &dst = g_VertexBuffer[i];
            dst.x = (src.pos.x - drawData->DisplayPos.x) * drawData->FramebufferScale.x;
            dst.y = (src.pos.y - drawData->DisplayPos.y) * drawData->FramebufferScale.y;
            dst.z = *g_pfNearZ;
            dst.rhw = *g_pfRecipNearClip;
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

            struct {
                float left, bottom, right, top;
            } clip{left, bottom, right, top};
            g_pfnSetScissor(*(CRect *)(void *)&clip);

            SetState(rwRENDERSTATETEXTURERASTER, static_cast<uintptr_t>(command.GetTexID()));
            g_pfnDrawIndexed(rwPRIMTYPETRILIST, g_VertexBuffer.data() + command.VtxOffset,
                             list->VtxBuffer.Size - static_cast<int>(command.VtxOffset),
                             list->IdxBuffer.Data + command.IdxOffset, static_cast<int>(command.ElemCount));

            struct {
                float left, bottom, right, top;
            } resetScissor{0.0f, 0.0f, 0.0f, 0.0f};
            g_pfnSetScissor(*(CRect *)(void *)&resetScissor);
        }
    }

    SetState(rwRENDERSTATETEXTURERASTER, 0);
    struct {
        float left, bottom, right, top;
    } resetScissor{0.0f, 0.0f, 0.0f, 0.0f};
    g_pfnSetScissor(*(CRect *)(void *)&resetScissor);

    if (g_pfnDefinedState2d)
        g_pfnDefinedState2d();

    return true;
}
