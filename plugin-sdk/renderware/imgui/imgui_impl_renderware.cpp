#include "imgui_impl_renderware.h"

#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/texture_loader.h>
#include <plugin-sdk/game_sa/Events.h>
#include <plugin-sdk/game_sa/base/Timer.h>
#include <plugin-sdk/game_sa/engine/RsGlobal.h>
#include <plugin-sdk/game_sa/plugin.h>
#include <plugin-sdk/gta_base/Rect.h>
#include <plugin-sdk/renderware/RwImage.h>
#include <plugin-sdk/renderware/RwRaster.h>
#include <plugin-sdk/renderware/RwRender.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace {
using DrawIndexedFn = RwBool (*)(i32, RwIm2DVertex *, i32, RwImVertexIndex *, i32);
using ScissorFn = void (*)(CRect &);
using DefinedState2dFn = void (*)();

struct TouchEvent {
    int type;
    int finger;
    float x;
    float y;
};

// Global state
bool g_bInitialized = false;
bool g_bMenuOpen = false;
bool g_bButtonVisible = true;
float g_CustomButtonW = 0.0f;
float g_CustomButtonH = 0.0f;
float g_CurrentButtonX = 0.0f;
float g_CurrentButtonY = 0.0f;
float g_CurrentButtonW = 0.0f;
float g_CurrentButtonH = 0.0f;

ImGuiContext *g_pImGuiContext = nullptr;
ImGui_RenderCallback g_UserRenderCallback = nullptr;
RwTexture *g_pButtonTexture = nullptr;
char g_szButtonPath[512] = {0};

std::mutex g_InputMutex;
std::vector<TouchEvent> g_TouchEvents;

// Game pointers
DrawIndexedFn g_pfnDrawIndexed = nullptr;
ScissorFn g_pfnSetScissor = nullptr;
DefinedState2dFn g_pfnDefinedState2d = nullptr;
void **g_ppEngine = nullptr;
void **g_ppRenderQueue = nullptr;
float *g_pfNearZ = nullptr;
float *g_pfRecipNearClip = nullptr;

// Hooks
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

void TryLoadButtonTexture() {
    if (g_pButtonTexture)
        return;

    if (g_szButtonPath[0]) {
        g_pButtonTexture = TextureLoader::LoadTextureFromFile(g_szButtonPath);
        if (g_pButtonTexture)
            return;
    }

    const char *dirs[] = {aml ? aml->GetAndroidDataPath() : nullptr, aml ? aml->GetConfigPath() : nullptr};
    const char *names[] = {"button.png", "button.jpg", "button.jpeg"};

    for (const auto *dir : dirs) {
        if (!dir || g_pButtonTexture)
            continue;
        for (const auto *name : names) {
            char path[512];
            if (std::snprintf(path, sizeof(path), "%s/%s", dir, name) > 0) {
                g_pButtonTexture = TextureLoader::LoadTextureFromFile(path);
                if (g_pButtonTexture)
                    return;
            }
        }
    }
}

// Touch event hook: automatically updates ImGui input and swallows interaction
void Hooked_AND_TouchEvent(int type, int finger, int x, int y) {
    if (!g_bInitialized) {
        if (orig_AND_TouchEvent)
            orig_AND_TouchEvent(type, finger, x, y);
        return;
    }

    float touchX = static_cast<float>(x);
    float touchY = static_cast<float>(y);

    {
        std::lock_guard<std::mutex> lock(g_InputMutex);
        g_TouchEvents.push_back({type, finger, touchX, touchY});
    }

    bool swallow = false;

    if (g_bMenuOpen) {
        // When menu is active, consume all touches so CJ/camera doesn't move
        swallow = true;
    } else if (g_bButtonVisible) {
        // When menu is closed, check if finger touched the floating Menu button
        if (touchX >= g_CurrentButtonX && touchX <= g_CurrentButtonX + g_CurrentButtonW && touchY >= g_CurrentButtonY &&
            touchY <= g_CurrentButtonY + g_CurrentButtonH) {
            swallow = true;
            if (type == 2 /* TOUCH_PUSH */) {
                g_bMenuOpen = true;
            }
        }
    }

    if (!swallow && orig_AND_TouchEvent) {
        orig_AND_TouchEvent(type, finger, x, y);
    }
}

void RenderFrame() {
    if (!g_bInitialized || !g_ppEngine || !*g_ppEngine || !g_ppRenderQueue || !*g_ppRenderQueue)
        return;

    RsGlobalType *pScreen = &RsGlobal;
    if (!pScreen || pScreen->maximumWidth <= 0 || pScreen->maximumHeight <= 0)
        return;

    const int width = pScreen->maximumWidth;
    const int height = pScreen->maximumHeight;

    // Prevent double rendering within the same game frame
    uint32_t currentTimerMS = CTimer::m_snTimeInMilliseconds;
    if (g_LastRenderFrame == currentTimerMS && currentTimerMS != 0)
        return;
    g_LastRenderFrame = currentTimerMS;

    TryLoadButtonTexture();

    // Calculate delta time
    uint32_t currentClockMS = CTimer::GetTimeMS();
    float deltaTime = 1.0f / 60.0f;
    if (g_LastRenderTime > 0 && currentClockMS >= g_LastRenderTime) {
        deltaTime = static_cast<float>(currentClockMS - g_LastRenderTime) / 1000.0f;
        if (deltaTime <= 0.0f || deltaTime > 0.1f)
            deltaTime = 1.0f / 60.0f;
    }
    g_LastRenderTime = currentClockMS;

    ImGui::SetCurrentContext(g_pImGuiContext);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = deltaTime;

    // Drain queued touch events
    {
        std::lock_guard<std::mutex> lock(g_InputMutex);
        for (const auto &ev : g_TouchEvents) {
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(ev.x, ev.y);
            if (ev.type == 2 /* TOUCH_PUSH */) {
                io.AddMouseButtonEvent(0, true);
            } else if (ev.type == 1 /* TOUCH_POP */ || ev.type == 4 /* TOUCH_CANCEL */) {
                io.AddMouseButtonEvent(0, false);
            }
        }
        g_TouchEvents.clear();
    }

    const float uiScale = std::clamp(static_cast<float>(height) / 720.0f, 0.8f, 2.0f);
    ImGui::GetStyle().FontSizeBase = 22.0f * uiScale;
    ImGui::GetStyle().FramePadding = ImVec2(10.0f * uiScale, 6.0f * uiScale);

    ImGui::NewFrame();

    // Calculate bottom-center button layout
    float btnW = (g_CustomButtonW > 0.0f) ? g_CustomButtonW : (100.0f * uiScale);
    float btnH = (g_CustomButtonH > 0.0f) ? g_CustomButtonH : (55.0f * uiScale);
    if (g_pButtonTexture && g_CustomButtonW <= 0.0f) {
        btnW = 72.0f * uiScale;
        btnH = 72.0f * uiScale;
    }

    g_CurrentButtonW = btnW;
    g_CurrentButtonH = btnH;
    g_CurrentButtonX = (static_cast<float>(width) - btnW) * 0.5f;
    g_CurrentButtonY = static_cast<float>(height) - btnH - (18.0f * uiScale);

    if (g_bButtonVisible) {
        ImGui::SetNextWindowPos(ImVec2(g_CurrentButtonX, g_CurrentButtonY));
        ImGui::SetNextWindowSize(ImVec2(btnW, btnH));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##FloatingLauncher", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings);

        bool pressed = false;
        if (g_pButtonTexture && g_pButtonTexture->raster) {
            const auto textureId = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(g_pButtonTexture->raster));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.2f, 0.4f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 0.6f));
            pressed = ImGui::ImageButton("##btnImg", ImTextureRef(textureId), ImVec2(btnW, btnH));
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
        } else {
            pressed = ImGui::Button("MENU", ImVec2(btnW, btnH));
        }

        if (pressed) {
            g_bMenuOpen = !g_bMenuOpen;
        }

        ImGui::End();
        ImGui::PopStyleVar();
    }

    // Call developer's UI callback when menu is opened
    if (g_bMenuOpen && g_UserRenderCallback) {
        g_UserRenderCallback();
    }

    ImGui::EndFrame();
    ImGui::Render();

    ImGui_ImplRenderWare_RenderDrawData(ImGui::GetDrawData());
}

void Hooked_MobileMenuRender(void *menu) {
    if (orig_MobileMenuRender)
        orig_MobileMenuRender(menu);
    RenderFrame();
}

void Hooked_ShutdownRenderWare() {
    ImGui_ImplRenderWare_Shutdown();
    if (orig_ShutdownRenderWare)
        orig_ShutdownRenderWare();
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

bool ImGui_ImplRenderWare_Init(ImGui_RenderCallback renderCallback, const char *buttonPath) {
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
    if (buttonPath && *buttonPath) {
        std::snprintf(g_szButtonPath, sizeof(g_szButtonPath), "%s", buttonPath);
    }

    if (!ImGui_ImplRenderWare_InitRenderer()) {
        ImGui::DestroyContext(g_pImGuiContext);
        g_pImGuiContext = nullptr;
        return false;
    }

    // Install touch hook
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

    // Hook shutdown
    void *pShutdown = reinterpret_cast<void *>(aml->GetSym(GetMainLibrary(), "_ZN5CGame18ShutdownRenderWareEv"));
    if (pShutdown) {
        aml->Hook(pShutdown, reinterpret_cast<void *>(&Hooked_ShutdownRenderWare),
                  reinterpret_cast<void **>(&orig_ShutdownRenderWare));
    }

    g_bInitialized = true;
    logger->Info("ImGui/RW: initialized successfully");
    return true;
}

void ImGui_ImplRenderWare_Shutdown() {
    if (!g_bInitialized)
        return;

    g_bMenuOpen = false;
    if (g_pButtonTexture) {
        RwTextureDestroy(g_pButtonTexture);
        g_pButtonTexture = nullptr;
    }

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

bool ImGui_ImplRenderWare_IsMenuOpen() { return g_bMenuOpen; }

void ImGui_ImplRenderWare_SetMenuOpen(bool open) { g_bMenuOpen = open; }

void ImGui_ImplRenderWare_ToggleMenu() { g_bMenuOpen = !g_bMenuOpen; }

void ImGui_ImplRenderWare_SetButtonVisible(bool visible) { g_bButtonVisible = visible; }

void ImGui_ImplRenderWare_SetButtonSize(float width, float height) {
    g_CustomButtonW = width;
    g_CustomButtonH = height;
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
