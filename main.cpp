#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/menu/menu.h>
#include <plugin-sdk/game_sa/Events.h>
#include <plugin-sdk/renderware/imgui/imgui_impl_renderware.h>
#include <src/gui.h>
#include <src/logic.h>
#include <src/menu_demo.h>

MYMOD(xavion.modtemplate, ModTemplate, 1.0, 0xavion)

ON_MOD_LOAD() {
    logger->SetTag("Mod Template");
    logger->Info("Mod Template loading...");

    ImGui_ImplRenderWare_Init(Gui::Draw);
    Menu::Init();
    MenuDemo::Init("Mod Template");
    Events::shutdownRwEvent += Gui::Shutdown;
    Events::shutdownRwEvent += Logic::Shutdown;

    logger->Info("Mod Template loaded successfully");
}
