#include <mod/amlmod.h>
#include <mod/logger.h>
#include <plugin-sdk/game_sa/engine/Radar.h>
#include <plugin-sdk/game_sa/engine/RsGlobal.h>
#include <plugin-sdk/game_sa/engine/Streaming.h>
#include <plugin-sdk/game_sa/engine/World.h>
#include <plugin-sdk/game_sa/entity/PlayerPed.h>
#include <plugin-sdk/game_sa/entity/Vehicle.h>
#include <plugin-sdk/game_sa/other/Clock.h>
#include <plugin-sdk/game_sa/other/Weather.h>
#include <plugin-sdk/game_sa/plugin.h>
#include <plugin-sdk/renderware/imgui/imgui_impl_renderware.h>

#include <cmath>

MYMOD(xavion.modtemplate, ModTemplate, 1.0, 0xavion)

namespace {
int g_nHour = 12;
int g_nMinute = 0;
int g_nWeatherIndex = 0;
const char *g_szStatus = "Ready";

void TeleportToWaypoint() {
    CPlayerPed *player = FindPlayerPed ? FindPlayerPed(-1) : nullptr;
    if (!player) {
        g_szStatus = "Player not loaded";
        return;
    }

    // Scan radar blips for the player's map waypoint
    tRadarTrace *traces = CRadar::ms_RadarTrace;
    tRadarTrace *waypoint = nullptr;
    if (traces) {
        for (int i = 0; i < 250; ++i) {
            if (traces[i].m_bInUse && traces[i].m_nBlipType == BLIP_COORD &&
                traces[i].m_nRadarSprite == RADAR_SPRITE_WAYPOINT) {
                waypoint = &traces[i];
                break;
            }
        }
    }

    if (!waypoint) {
        g_szStatus = "Place a waypoint on map first!";
        return;
    }

    CVector dest = waypoint->m_vecPos;
    dest.z = 1000.0f;

    // Preload collision at destination
    CStreaming::LoadSceneCollision(dest);
    CStreaming::LoadScene(dest);

    bool found = false;
    CEntity *ground = nullptr;
    float z = CWorld::FindGroundZFor3DCoord(dest.x, dest.y, dest.z, &found, &ground);
    if (found && std::isfinite(z)) {
        dest.z = z + 1.5f;
    } else {
        dest.z = 20.0f;
    }

    CVehicle *veh = FindPlayerVehicle ? FindPlayerVehicle(-1, false) : nullptr;
    if (veh) {
        dest.z += 1.0f;
        veh->Teleport(dest, false);
    } else {
        player->Teleport(dest, false);
    }

    g_szStatus = "Teleported to waypoint!";
}

void DrawTrainerUI() {
    float scale = std::clamp(static_cast<float>(RsGlobal.maximumHeight) / 720.0f, 0.8f, 2.0f);
    ImGui::SetNextWindowSize(ImVec2(460.0f * scale, 340.0f * scale), ImGuiCond_FirstUseEver);

    bool open = ImGui_ImplRenderWare_IsMenuOpen();
    if (ImGui::Begin("Simple Trainer###TrainerWindow", &open, ImGuiWindowFlags_NoCollapse)) {
        CPlayerPed *player = FindPlayerPed ? FindPlayerPed(-1) : nullptr;

        if (ImGui::BeginTabBar("TrainerTabs")) {
            // Time
            if (ImGui::BeginTabItem("Time")) {
                ImGui::SliderInt("Hour", &g_nHour, 0, 23);
                ImGui::SliderInt("Minute", &g_nMinute, 0, 59);

                ImGui::BeginDisabled(!player);
                if (ImGui::Button("Set Time", ImVec2(-1, 40.0f * scale))) {
                    CClock::SetGameClock(static_cast<u8>(g_nHour), static_cast<u8>(g_nMinute), 0);
                    g_szStatus = "Time changed!";
                }

                ImGui::Separator();
                if (ImGui::Button("Day (12:00)", ImVec2(105.0f * scale, 36.0f * scale))) {
                    g_nHour = 12;
                    g_nMinute = 0;
                    CClock::SetGameClock(12, 0, 0);
                }
                ImGui::SameLine();
                if (ImGui::Button("Night (00:00)", ImVec2(105.0f * scale, 36.0f * scale))) {
                    g_nHour = 0;
                    g_nMinute = 0;
                    CClock::SetGameClock(0, 0, 0);
                }
                ImGui::SameLine();
                if (ImGui::Button("Morning (08:00)", ImVec2(105.0f * scale, 36.0f * scale))) {
                    g_nHour = 8;
                    g_nMinute = 0;
                    CClock::SetGameClock(8, 0, 0);
                }
                ImGui::EndDisabled();

                ImGui::EndTabItem();
            }

            // Weather
            if (ImGui::BeginTabItem("Weather")) {
                const char *weathers[] = {"Sunny (LA)", "Extra Sunny (LA)", "Cloudy (LA)", "Sunny (SF)",
                                          "Rainy (SF)", "Foggy (SF)",       "Desert",      "Sandstorm"};
                static const int weatherIDs[] = {WEATHER_SUNNY_LA,     WEATHER_EXTRASUNNY_LA,   WEATHER_CLOUDY_LA,
                                                 WEATHER_SUNNY_SF,     WEATHER_RAINY_SF,        WEATHER_FOGGY_SF,
                                                 WEATHER_SUNNY_DESERT, WEATHER_SANDSTORM_DESERT};

                ImGui::Combo("Preset", &g_nWeatherIndex, weathers, IM_ARRAYSIZE(weathers));

                ImGui::BeginDisabled(!player);
                if (ImGui::Button("Set Weather", ImVec2(-1, 40.0f * scale))) {
                    CWeather::ForceWeatherNow(static_cast<i16>(weatherIDs[g_nWeatherIndex]));
                    g_szStatus = "Weather changed!";
                }

                if (ImGui::Button("Restore Natural Weather", ImVec2(-1, 36.0f * scale))) {
                    CWeather::ReleaseWeather();
                    g_szStatus = "Natural weather restored!";
                }
                ImGui::EndDisabled();

                ImGui::EndTabItem();
            }

            // Teleport
            if (ImGui::BeginTabItem("Teleport")) {
                ImGui::TextWrapped("Place a waypoint on the pause map, then tap the button below to teleport.");
                ImGui::Spacing();

                ImGui::BeginDisabled(!player);
                if (ImGui::Button("Teleport to Waypoint", ImVec2(-1, 48.0f * scale))) {
                    TeleportToWaypoint();
                }
                ImGui::EndDisabled();

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Status: %s", g_szStatus);
        if (!player) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "(In-game world not loaded)");
        }
    }
    ImGui::End();

    if (!open) {
        ImGui_ImplRenderWare_SetMenuOpen(false);
    }
}
} // namespace

ON_MOD_LOAD() {
    logger->SetTag("Mod Template");
    logger->Info("Mod Template loading...");

    ImGui_ImplRenderWare_Init(DrawTrainerUI);

    logger->Info("Mod Template loaded successfully");
}
