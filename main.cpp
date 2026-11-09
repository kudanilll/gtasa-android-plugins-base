#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/texture_loader.h>
#include <plugin-sdk/game_sa/Events.h>
#include <plugin-sdk/game_sa/base/Timer.h>
#include <plugin-sdk/game_sa/engine/Radar.h>
#include <plugin-sdk/game_sa/engine/RsGlobal.h>
#include <plugin-sdk/game_sa/engine/Streaming.h>
#include <plugin-sdk/game_sa/engine/World.h>
#include <plugin-sdk/game_sa/entity/PlayerPed.h>
#include <plugin-sdk/game_sa/entity/Vehicle.h>
#include <plugin-sdk/game_sa/other/Clock.h>
#include <plugin-sdk/game_sa/other/Weather.h>
#include <plugin-sdk/renderware/RwCamera.h>
#include <plugin-sdk/renderware/imgui/imgui_impl_renderware.h>
#include <plugin-sdk/renderware/imgui/imgui_impl_renderware_internal.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>

MYMOD(xavion.modtemplate, ModTemplate, 1.0, 0xavion)

namespace {
RsGlobalType *screen = nullptr;
tRadarTrace *radarTraces = nullptr;
bool *userPause = nullptr;
bool *codePause = nullptr;
bool *cutscene = nullptr;
int (*windowWidth)() = nullptr;
int (*windowHeight)() = nullptr;
void (*originalTouch)(int, int, int, int) = nullptr;
void (*originalMobileMenuRender)(void *) = nullptr;
void (*originalShutdown)() = nullptr;

ImGuiContext *context = nullptr;
RwTexture *buttonTexture = nullptr;
bool showMenu = false;
bool initializationFailed = false;
bool teleportRequested = false;
int hour = 12, minute = 0, weather = 0;
const char *message = "";
std::atomic<bool> uiVisible{false};
std::atomic<bool> captureScreen{false};
std::atomic<uint64_t> renderExtent{0};
std::chrono::steady_clock::time_point lastFrame;

float UIScale(int height) { return std::clamp(height / 720.0f, 0.75f, 2.0f); }

ImVec4 LauncherBounds(int width, int height) {
    const float scale = UIScale(height);
    const float size = std::min(96.0f * scale, std::min(width, height) * 0.5f);
    const float left = (width - size) * 0.5f;
    const float bottom = height - 24.0f * scale;
    return {left, bottom - size, left + size, bottom};
}

void TouchHook(int type, int finger, int x, int y) {
    const auto extent = renderExtent.load();
    const int width = static_cast<int>(extent >> 32);
    const int height = static_cast<int>(extent & 0xFFFFFFFFu);
    const auto point = ImGuiRenderWareDetail::MapTouchToRender(x, y, windowWidth ? windowWidth() : width,
                                                               windowHeight ? windowHeight() : height, width, height);
    const auto bounds = LauncherBounds(width, height);
    const bool overButton = point.x >= bounds.x && point.x < bounds.z && point.y >= bounds.y && point.y < bounds.w;
    const bool capture = uiVisible.load() && (captureScreen.load() || overButton);
    if (!ImGui_ImplRenderWare_ProcessTouch(type, finger, point.x, point.y, capture) && originalTouch)
        originalTouch(type, finger, x, y);
}

const char *TeleportToMarker() {
    auto *player = FindPlayerPed ? FindPlayerPed(-1) : nullptr;
    if (!player)
        return "Load a game first.";
    if (cutscene && *cutscene)
        return "Finish the cutscene first.";
    if (player->m_nAreaCode != 0)
        return "Go outside before teleporting.";
    if (!radarTraces || !FindPlayerVehicle || !CStreaming::LoadSceneCollision || !CStreaming::LoadScene ||
        !CWorld::FindGroundZFor3DCoord || !CTimer::Stop || !CTimer::Update)
        return "Teleport is unavailable on this game build.";

    const auto *marker = std::find_if(radarTraces, radarTraces + 250, [](const tRadarTrace &trace) {
        return trace.m_bInUse && trace.m_nBlipType == BLIP_COORD && trace.m_nRadarSprite == RADAR_SPRITE_WAYPOINT;
    });
    if (marker == radarTraces + 250)
        return "Place a waypoint on the map first.";
    CVector destination = marker->m_vecPos;
    if (!std::isfinite(destination.x) || !std::isfinite(destination.y))
        return "Invalid waypoint.";

    destination.z = 1000.0f;
    CTimer::Stop();
    CStreaming::LoadSceneCollision(destination);
    CStreaming::LoadScene(destination);
    CTimer::Update();
    bool found = false;
    CEntity *ground = nullptr;
    const float groundZ = CWorld::FindGroundZFor3DCoord(destination.x, destination.y, destination.z, &found, &ground);
    if (!found || !std::isfinite(groundZ))
        return "No safe ground found at the waypoint.";
    auto *vehicle = FindPlayerVehicle(-1, false);
    CEntity *entity = vehicle ? static_cast<CEntity *>(vehicle) : static_cast<CEntity *>(player);
    destination.z = groundZ + (vehicle ? 3.0f : 1.0f);
    entity->Teleport(destination, false);
    return "Teleported to waypoint.";
}

void ProcessTrainer() {
    if (teleportRequested && !(userPause && *userPause) && !(codePause && *codePause)) {
        teleportRequested = false;
        message = TeleportToMarker();
    }
}

void DrawTrainer(float scale, const ImVec4 &launcher) {
    const bool playing = FindPlayerPed && FindPlayerPed(-1);
    ImGui::SetNextWindowPos(ImVec2(screen->maximumWidth * 0.5f, launcher.y - 12.0f * scale), ImGuiCond_FirstUseEver,
                            ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(std::min(480.0f * scale, screen->maximumWidth - 24.0f * scale),
                                    std::min(340.0f * scale, launcher.y - 24.0f * scale)),
                             ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Simple Trainer", &showMenu, ImGuiWindowFlags_NoCollapse)) {
        if (ImGui::BeginTabBar("Trainer")) {
            if (ImGui::BeginTabItem("Time")) {
                ImGui::SliderInt("Hour", &hour, 0, 23);
                ImGui::SliderInt("Minute", &minute, 0, 59);
                ImGui::BeginDisabled(!playing || !CClock::SetGameClock);
                if (ImGui::Button("Set time", ImVec2(-1, 48.0f * scale))) {
                    // A zero day keeps the calendar unchanged.
                    CClock::SetGameClock(static_cast<u8>(hour), static_cast<u8>(minute), 0);
                    message = "Time changed.";
                }
                ImGui::EndDisabled();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Weather")) {
                constexpr i16 types[] = {WEATHER_EXTRASUNNY_LA, WEATHER_SUNNY_LA, WEATHER_CLOUDY_LA,
                                         WEATHER_RAINY_SF,      WEATHER_FOGGY_SF, WEATHER_SANDSTORM_DESERT};
                ImGui::Combo("Weather", &weather, "Extra sunny\0Sunny\0Cloudy\0Rainy\0Foggy\0Sandstorm\0");
                ImGui::BeginDisabled(!playing || !CWeather::ForceWeatherNow);
                if (ImGui::Button("Set weather", ImVec2(-1, 48.0f * scale))) {
                    CWeather::ForceWeatherNow(types[weather]);
                    message = "Weather changed.";
                }
                ImGui::EndDisabled();
                ImGui::BeginDisabled(!playing || !CWeather::ReleaseWeather);
                if (ImGui::Button("Natural weather", ImVec2(-1, 48.0f * scale))) {
                    CWeather::ReleaseWeather();
                    message = "Natural weather restored.";
                }
                ImGui::EndDisabled();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Teleport")) {
                ImGui::TextWrapped("Place a waypoint on the map, then teleport to it.");
                ImGui::BeginDisabled(!playing || teleportRequested);
                if (ImGui::Button("Teleport to marker", ImVec2(-1, 48.0f * scale))) {
                    teleportRequested = true;
                    message = "Teleport queued. Resume gameplay if paused.";
                }
                ImGui::EndDisabled();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::Separator();
        ImGui::TextWrapped("%s", playing ? message : "Start or load a game to use the trainer.");
    }
    ImGui::End();
}

void RenderUI() {
    if (!screen || screen->maximumWidth <= 0 || screen->maximumHeight <= 0 || initializationFailed)
        return;
    const int width = screen->maximumWidth, height = screen->maximumHeight;
    renderExtent = (static_cast<uint64_t>(width) << 32) | static_cast<uint32_t>(height);
    const auto previous = ImGui::GetCurrentContext();
    if (!context) {
        IMGUI_CHECKVERSION();
        context = ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().LogFilename = nullptr;
        ImGui::StyleColorsDark();
        if (!ImGui_ImplRenderWare_Init()) {
            ImGui::DestroyContext(context);
            context = nullptr;
            initializationFailed = true;
            ImGui::SetCurrentContext(previous);
            return;
        }
        const char *dirs[] = {aml->GetAndroidDataPath(), aml->GetConfigPath()};
        const char *names[] = {"button.png", "button.jpg", "button.jpeg"};
        for (const auto *dir : dirs) {
            if (!dir || buttonTexture)
                continue;
            for (const auto *name : names) {
                char path[512];
                if (std::snprintf(path, sizeof(path), "%s/%s", dir, name) > 0) {
                    buttonTexture = TextureLoader::LoadTextureFromFile(path);
                    if (buttonTexture)
                        break;
                }
            }
        }
        lastFrame = std::chrono::steady_clock::now();
    }
    ImGui::SetCurrentContext(context);
    const auto now = std::chrono::steady_clock::now();
    const float delta = std::chrono::duration<float>(now - lastFrame).count();
    lastFrame = now;
    if (ImGui_ImplRenderWare_NewFrame(width, height, delta)) {
        const float scale = UIScale(height);
        auto &style = ImGui::GetStyle();
        style.FontSizeBase = 24.0f * scale;
        style.FramePadding = ImVec2(12.0f * scale, 8.0f * scale);
        ImGui::NewFrame();

        const auto bounds = LauncherBounds(width, height);
        const ImVec2 size(bounds.z - bounds.x, bounds.w - bounds.y);
        ImGui::SetNextWindowPos(ImVec2(bounds.x, bounds.y));
        ImGui::SetNextWindowSize(size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##launcher", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings);
        bool pressed = false;
        if (buttonTexture && buttonTexture->raster) {
            const auto texture = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(buttonTexture->raster));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 0.5f));
            pressed = ImGui::ImageButton("##open", ImTextureRef(texture), size);
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
        } else {
            pressed = ImGui::Button("Menu", size);
        }
        if (pressed)
            showMenu = !showMenu;
        ImGui::End();
        ImGui::PopStyleVar();
        if (showMenu)
            DrawTrainer(scale, bounds);

        ImGui::Render();
        const bool rendered = ImGui_ImplRenderWare_RenderDrawData(ImGui::GetDrawData());
        uiVisible = rendered;
        captureScreen = rendered && showMenu;
        ImGui_ImplRenderWare_SetInputEnabled(rendered);
    } else {
        uiVisible = false;
        captureScreen = false;
        ImGui_ImplRenderWare_SetInputEnabled(false);
    }
    ImGui::SetCurrentContext(previous);
}

void MobileMenuRenderHook(void *menu) {
    if (originalMobileMenuRender)
        originalMobileMenuRender(menu);
    RenderUI();
}

void ShutdownHook() {
    uiVisible = false;
    captureScreen = false;
    teleportRequested = false;
    ImGui_ImplRenderWare_SetInputEnabled(false);
    if (context) {
        const auto previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(context);
        if (buttonTexture) {
            RwTextureDestroy(buttonTexture);
            buttonTexture = nullptr;
        }
        ImGui_ImplRenderWare_Shutdown();
        ImGui::DestroyContext(context);
        ImGui::SetCurrentContext(previous == context ? nullptr : previous);
        context = nullptr;
    }
    showMenu = false;
    originalShutdown();
}
} // namespace

ON_MOD_LOAD() {
    logger->SetTag("Mod Template");
    screen = GetMainLibrarySymbol<RsGlobalType *>("RsGlobal");
    radarTraces = GetMainLibrarySymbol<tRadarTrace *>("_ZN6CRadar13ms_RadarTraceE");
    userPause = GetMainLibrarySymbol<bool *>("_ZN6CTimer11m_UserPauseE");
    codePause = GetMainLibrarySymbol<bool *>("_ZN6CTimer11m_CodePauseE");
    cutscene = GetMainLibrarySymbol<bool *>("_ZN12CCutsceneMgr10ms_runningE");
    windowWidth = GetMainLibrarySymbol<int (*)()>("_Z17OS_ScreenGetWidthv");
    windowHeight = GetMainLibrarySymbol<int (*)()>("_Z18OS_ScreenGetHeightv");
    const auto touch = GetMainLibrarySymbol<void *>("_Z14AND_TouchEventiiii");
    const auto shutdown = GetMainLibrarySymbol<void *>("_ZN5CGame18ShutdownRenderWareEv");
    if (!screen || !touch || !shutdown || !GetMainLibrarySymbol<void *>("_ZN5CGame7ProcessEv")) {
        logger->Error("Required game symbol missing; template disabled");
        return;
    }
    Events::gameProcessEvent.before += ProcessTrainer;
    Events::drawingEvent.after += RenderUI;
    const auto mobileMenuRender = GetMainLibrarySymbol<void *>("_ZN10MobileMenu6RenderEv");
    if (mobileMenuRender) {
        aml->Hook(mobileMenuRender, reinterpret_cast<void *>(&MobileMenuRenderHook),
                  reinterpret_cast<void **>(&originalMobileMenuRender));
    }
    if (!aml->Hook(shutdown, reinterpret_cast<void *>(&ShutdownHook), reinterpret_cast<void **>(&originalShutdown)) ||
        !aml->Hook(touch, reinterpret_cast<void *>(&TouchHook), reinterpret_cast<void **>(&originalTouch))) {
        logger->Error("Failed to install template hooks");
        return;
    }
    logger->Info("Simple Trainer loaded");
}
