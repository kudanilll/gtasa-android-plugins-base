#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <mod/thirdparty/imgui/imgui.h>
#include <plugin-sdk/renderware/imgui/imgui_impl_renderware_internal.h>

int main() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 720);
    io.DeltaTime = 1.0f / 60;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
    ImGui::GetPlatformIO().Renderer_TextureMaxWidth = 4096;
    ImGui::GetPlatformIO().Renderer_TextureMaxHeight = 4096;
    ImGuiRenderWareDetail::TouchInput input;
    input.SetEnabled(true);
    int clicks = 0, uploads = 0;
    const auto frame = [&]() {
        for (const auto &event : input.Drain()) {
            io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
            io.AddMousePosEvent(static_cast<float>(event.x), static_cast<float>(event.y));
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, event.type == 2);
        }
        ImGui::NewFrame();
        const float scale = std::clamp(io.DisplaySize.y / 720.0f, 0.75f, 2.0f);
        const float size = 96.0f * scale;
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 24.0f * scale), ImGuiCond_Always,
                                ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowSize(ImVec2(size, size));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("launcher", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings);
        if (ImGui::Button("Menu", ImVec2(size, size)))
            ++clicks;
        const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        assert(std::abs((min.x + max.x) * 0.5f - io.DisplaySize.x * 0.5f) < 1.0f);
        assert(max.y <= io.DisplaySize.y && max.y > io.DisplaySize.y * 0.75f);
        assert(max.x - min.x > 64.0f);
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::Render();
        auto *data = ImGui::GetDrawData();
        assert(data->Valid && data->TotalVtxCount > 0);
        for (auto *texture : *data->Textures) {
            if (texture->Status == ImTextureStatus_WantCreate || texture->Status == ImTextureStatus_WantUpdates) {
                assert(texture->Format == ImTextureFormat_RGBA32 && texture->GetPixels() && texture->GetPitch() > 0);
                texture->SetTexID(static_cast<ImTextureID>(texture->UniqueID + 1));
                texture->SetStatus(ImTextureStatus_OK);
                ++uploads;
            }
        }
        for (const auto *list : data->CmdLists)
            for (const auto &command : list->CmdBuffer)
                if (!command.UserCallback)
                    assert(command.GetTexID() != ImTextureID_Invalid);
    };
    frame();
    frame();
    const auto tap = ImGuiRenderWareDetail::MapTouchToRender(960, 972, 1920, 1080, 1280, 720);
    assert(tap.x == 640 && tap.y == 648);
    assert(input.Process(2, 0, tap.x, tap.y, true));
    assert(input.Process(1, 0, tap.x, tap.y, false));
    frame();
    frame();
    frame();
    assert(clicks == 1 && uploads > 0); // A complete tap between frames still clicks exactly once.

    io.DisplaySize = ImVec2(1920, 1080);
    frame();
    io.DisplaySize = ImVec2(2560, 1080);
    frame(); // Bottom-center placement remains inside widescreen/ultrawide viewports.

    ImGui::NewFrame();
    auto *largeMesh = ImGui::GetForegroundDrawList();
    for (int i = 0; i < 20000; ++i)
        largeMesh->AddRectFilled(ImVec2(1, 1), ImVec2(2, 2), IM_COL32_WHITE);
    ImGui::Render();
    bool hasVertexOffset = false;
    for (const auto *list : ImGui::GetDrawData()->CmdLists) {
        for (const auto &command : list->CmdBuffer) {
            hasVertexOffset |= command.VtxOffset > 0;
            for (unsigned i = 0; i < command.ElemCount; ++i)
                assert(list->IdxBuffer[command.IdxOffset + i] + command.VtxOffset <
                       static_cast<unsigned>(list->VtxBuffer.Size));
        }
    }
    assert(hasVertexOffset); // >64K vertices require per-command VtxOffset with 16-bit indices.
    for (auto *texture : ImGui::GetPlatformIO().Textures) {
        texture->SetTexID(ImTextureID_Invalid);
        texture->SetStatus(ImTextureStatus_Destroyed);
    }
    ImGui::DestroyContext();
    std::printf("PASS: ImGui %s texture API, short-tap button input, >64K-vertex draw offsets\n", IMGUI_VERSION);
}
