#include <mod/amlmod.h>
#include <mod/logger.h>
#include <plugin-sdk/game_sa/Events.h>
#include <plugin-sdk/renderware/imgui/imgui_impl_renderware.h>
#include <src/gui.h>
#include <src/logic.h>

MYMOD(xavion.modtemplate, ModTemplate, 1.0, 0xavion)

ON_MOD_LOAD() {
    logger->SetTag("Mod Template");
    logger->Info("Mod Template loading...");

    ImGui_ImplRenderWare_Init(Gui::Draw);
    Events::shutdownRwEvent += Gui::Shutdown;
    Events::shutdownRwEvent += Logic::Shutdown;

    logger->Info("Mod Template loaded successfully");
}
