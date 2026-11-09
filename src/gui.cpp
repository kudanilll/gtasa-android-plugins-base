#include "gui.h"

#include "logic.h"

#include <mod/logger.h>
#include <mod/music_player.h>
#include <mod/texture_loader.h>
#include <plugin-sdk/game_sa/engine/RsGlobal.h>
#include <plugin-sdk/renderware/imgui/imgui_impl_renderware.h>

#include <algorithm>
#include <string>

namespace {
bool g_bMenuOpen = false;
int g_nHour = 12;
int g_nMinute = 0;
int g_nWeatherIndex = 0;
float g_fMusicVolume = 1.0f;
bool g_bMusicLoop = true;

const char *g_szTextBox = nullptr; // Points at a string literal.
float g_fTextBoxSecondsLeft = 0.0f;
bool g_bLoadedMessageShown = false;

RwTexture *g_pButtonTexture = nullptr;
bool g_bButtonLoadTried = false;

ImFont *g_pFont = nullptr; // Owned by this plugin's ImGui font atlas.
bool g_bFontLoadTried = false;

void LoadButtonTextureOnce() {
    if (g_bButtonLoadTried)
        return;
    g_bButtonLoadTried = true;

    const std::string path = Logic::FindAsset({"button.png", "button.jpg", "button.jpeg"});
    if (!path.empty())
        g_pButtonTexture = TextureLoader::LoadTextureFromFile(path.c_str());
}

// ImGui 1.92 rasterizes glyphs on demand at any size, so one font file serves every UI scale.
ImFont *LoadFontOnce() {
    if (g_bFontLoadTried)
        return g_pFont;
    g_bFontLoadTried = true;

    const std::string path = Logic::FindAsset({"font.ttf", "font.otf"});
    if (path.empty()) {
        logger->Info("font.ttf/font.otf not found; using ImGui's default font");
        return nullptr;
    }

    ImFontConfig config;
    config.Flags |= ImFontFlags_NoLoadError; // Report failures through our logger instead of ImGui's assert path.
    g_pFont = ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), 0.0f, &config);
    if (g_pFont)
        logger->Info("Loaded font %s", path.c_str());
    else
        logger->Error("Failed to load font %s", path.c_str());
    return g_pFont;
}

// Like CLEO's show_text_box: a short message in the top-left corner.
void ShowTextBox(const char *text) {
    g_szTextBox = text;
    g_fTextBoxSecondsLeft = 3.0f;
}

void DrawTextBox(float scale) {
    if (g_fTextBoxSecondsLeft <= 0.0f)
        return;
    g_fTextBoxSecondsLeft -= ImGui::GetIO().DeltaTime;

    ImGui::SetNextWindowPos(ImVec2(40.0f * scale, 40.0f * scale));
    ImGui::SetNextWindowBgAlpha(0.75f);
    // NoInputs keeps the box out of the touch rects, so taps on it still reach the game.
    if (ImGui::Begin("##TextBox", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::PushTextWrapPos(380.0f * scale);
        ImGui::TextUnformatted(g_szTextBox);
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
}

// The backend has no built-in menu. This launcher is example policy: move, restyle or remove it.
void DrawLauncher(float scale) {
    LoadButtonTextureOnce();
    const bool hasImage = g_pButtonTexture && g_pButtonTexture->raster;
    const ImVec2 size = hasImage ? ImVec2(84.0f * scale, 84.0f * scale) : ImVec2(120.0f * scale, 58.0f * scale);

    ImGui::SetNextWindowPos(
        ImVec2((RsGlobal.maximumWidth - size.x) * 0.5f, RsGlobal.maximumHeight - size.y - 18.0f * scale));
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("##Launcher", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings)) {
        bool pressed = false;
        if (hasImage) {
            // The RenderWare backend uses RwRaster * as ImTextureID.
            const auto textureId = static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(g_pButtonTexture->raster));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.2f, 0.4f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 0.6f));
            pressed = ImGui::ImageButton("##LauncherImage", ImTextureRef(textureId), size);
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
        } else {
            pressed = ImGui::Button("MENU", size);
        }
        if (pressed)
            g_bMenuOpen = !g_bMenuOpen;
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void DrawTimeTab(float scale) {
    ImGui::SliderInt("Hour", &g_nHour, 0, 23);
    ImGui::SliderInt("Minute", &g_nMinute, 0, 59);

    ImGui::BeginDisabled(!Logic::IsPlayerReady());
    if (ImGui::Button("Set Time", ImVec2(-1, 40.0f * scale))) {
        Logic::SetTime(g_nHour, g_nMinute);
        ShowTextBox("Time changed!");
    }

    ImGui::Separator();
    const struct {
        const char *label;
        int hour;
    } presets[] = {{"Day (12:00)", 12}, {"Night (00:00)", 0}, {"Morning (08:00)", 8}};
    for (size_t i = 0; i < IM_ARRAYSIZE(presets); ++i) {
        if (i > 0)
            ImGui::SameLine();
        if (ImGui::Button(presets[i].label, ImVec2(105.0f * scale, 36.0f * scale))) {
            g_nHour = presets[i].hour;
            g_nMinute = 0;
            Logic::SetTime(g_nHour, g_nMinute);
        }
    }
    ImGui::EndDisabled();
}

void DrawWeatherTab(float scale) {
    ImGui::Combo("Preset", &g_nWeatherIndex, Logic::weatherPresetNames, Logic::weatherPresetCount);

    ImGui::BeginDisabled(!Logic::IsPlayerReady());
    if (ImGui::Button("Set Weather", ImVec2(-1, 40.0f * scale))) {
        Logic::SetWeatherPreset(g_nWeatherIndex);
        ShowTextBox("Weather changed!");
    }
    if (ImGui::Button("Restore Natural Weather", ImVec2(-1, 36.0f * scale))) {
        Logic::RestoreWeather();
        ShowTextBox("Natural weather restored!");
    }
    ImGui::EndDisabled();
}

void DrawTeleportTab(float scale) {
    ImGui::TextWrapped("Place a waypoint on the pause map, then tap the button below to teleport.");
    ImGui::Spacing();

    ImGui::BeginDisabled(!Logic::IsPlayerReady());
    if (ImGui::Button("Teleport to Waypoint", ImVec2(-1, 48.0f * scale)))
        ShowTextBox(Logic::TeleportToWaypoint());
    ImGui::EndDisabled();
}

void DrawMusicTab(float scale) {
    MusicPlayer &music = Logic::Music();
    const bool playing = music.IsPlaying();
    if (ImGui::Button(playing ? "Pause" : "Play", ImVec2(160.0f * scale, 40.0f * scale))) {
        if (playing)
            music.Pause();
        else if (const char *error = Logic::PlayMusic(g_fMusicVolume, g_bMusicLoop))
            ShowTextBox(error);
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop", ImVec2(160.0f * scale, 40.0f * scale)))
        music.Stop();

    if (ImGui::SliderFloat("Volume", &g_fMusicVolume, 0.0f, 1.0f))
        music.SetVolume(g_fMusicVolume);
    if (ImGui::Checkbox("Loop", &g_bMusicLoop))
        music.SetLooping(g_bMusicLoop);
}

void DrawTrainerUI(float scale) {
    ImGui::SetNextWindowPos(ImVec2(RsGlobal.maximumWidth * 0.5f, RsGlobal.maximumHeight * 0.45f),
                            ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460.0f * scale, 340.0f * scale), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Simple Trainer###TrainerWindow", &g_bMenuOpen, ImGuiWindowFlags_NoCollapse)) {
        if (ImGui::BeginTabBar("TrainerTabs")) {
            const struct {
                const char *label;
                void (*draw)(float);
            } tabs[] = {{"Time", DrawTimeTab},
                        {"Weather", DrawWeatherTab},
                        {"Teleport", DrawTeleportTab},
                        {"Music", DrawMusicTab}};
            for (const auto &tab : tabs) {
                if (ImGui::BeginTabItem(tab.label)) {
                    tab.draw(scale);
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }

        if (!Logic::IsPlayerReady()) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "(In-game world not loaded)");
        }
    }
    ImGui::End();
}
} // namespace

namespace Gui {
void Draw() {
    const float scale = std::clamp(static_cast<float>(RsGlobal.maximumHeight) / 720.0f, 0.8f, 2.0f);
    DrawLauncher(scale);

    // First spawn in-game. Drawn with ImGui instead of CSprite2d/CFont so it can use the custom font.
    if (!g_bLoadedMessageShown && Logic::IsPlayerReady()) {
        g_bLoadedMessageShown = true;
        ShowTextBox("mod loaded!");
    }

    // A null font keeps ImGui's default font; size 0 keeps the size the backend set for this frame.
    ImGui::PushFont(LoadFontOnce(), 0.0f);
    DrawTextBox(scale);
    if (g_bMenuOpen)
        DrawTrainerUI(scale);
    ImGui::PopFont();
}

void Shutdown() {
    if (g_pButtonTexture)
        RwTextureDestroy(g_pButtonTexture);
    g_pButtonTexture = nullptr;
    g_bButtonLoadTried = false; // RenderWare may start again; reload the button then.
    g_pFont = nullptr;          // Freed with the ImGui context when the backend shuts down.
    g_bFontLoadTried = false;
}
} // namespace Gui
