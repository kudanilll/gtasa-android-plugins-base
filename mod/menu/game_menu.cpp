#include "game_menu.h"

#include <mod/logger.h>
#include <plugin-sdk/game_sa/engine/MobileMenu.h>
#include <plugin-sdk/renderware/RwTexture.h>
#include <plugin-sdk/sdk_base.h>

#include <cstddef>
#include <type_traits>

// Layout checks. The game passes CVector2D in float registers, so ours must stay a plain struct.
static_assert(std::is_trivially_copyable_v<CVector2D> && sizeof(CVector2D) == 8);
static_assert(sizeof(MenuGame::FlowItem) == 3 * sizeof(void *));
static_assert(offsetof(RwTexture, refCount) == MenuGame::kTextureRefCount);
static_assert(MenuGame::kScreenSlots == 21 && MenuGame::kSelectionSlots == 9);

namespace MenuGame {
namespace {

MobileMenu *g_pMobileMenu = nullptr;

// Looks a symbol up into `out`; logs and fails on the first miss.
template <class T> bool Find(T &out, const char *symbol) {
    out = reinterpret_cast<T>(GetMainLibrarySymbol<void *>(symbol));
    if (!out)
        logger->Error("Menu: missing symbol %s, menus disabled", symbol);
    return out != nullptr;
}

// Vtable symbols point at offset-to-top; objects point two slots further.
bool FindVTable(void **&out, const char *symbol) {
    if (!Find(out, symbol))
        return false;
    out += 2;
    return true;
}

bool ResolveInto(Api &a) {
    return Find(a.asciiToGxt, "_Z14AsciiToGxtCharPKcPt") &&
           Find(a.getTexture, "_ZN22TextureDatabaseRuntime10GetTextureEPKc") &&
           Find(a.addItem, "_ZN12SelectScreen7AddItemEPNS_13MenuSelectionE") &&
           Find(a.selectionRender, "_ZN12SelectScreen13MenuSelection6RenderEPS_9CVector2DS2_f") &&
           Find(a.drawTextDirect, "_ZN10MenuScreen14DrawTextDirectEPt9TextAligni5CRGBAb9CVector2DS3_bPi") &&
           Find(a.drawSlider, "_ZN10MenuScreen10DrawSliderE9CVector2DS0_fbhb") &&
           Find(a.charSelectCtor, "_ZN16CharSelectScreenC2EPKcb") && Find(a.flowCtor, "_ZN10FlowScreenC2Ebb") &&
           Find(a.pushScreen, "_ZN10MenuScreen10PushScreenEPS_b") &&
           Find(a.reportFrontendAudio, "_ZN12CAudioEngine24ReportFrontendAudioEventEiff") &&
           Find(a.hidImplements, "_ZN4CHID10ImplementsE10HIDMapping") &&
           Find(a.hidIsReleased, "_ZN4CHID10IsReleasedE10HIDMapping") &&
           Find(a.screenWidth, "_Z17OS_ScreenGetWidthv") && Find(a.screenHeight, "_Z18OS_ScreenGetHeightv") &&
           Find(a.audioEngine, "AudioEngine") && Find(a.lastDevice, "lastDevice") &&
           Find(g_pMobileMenu, "gMobileMenu") && FindVTable(a.vtCharSelect, "_ZTV16CharSelectScreen") &&
           FindVTable(a.vtStartGame, "_ZTV15StartGameScreen") && FindVTable(a.vtSettings, "_ZTV14SettingsScreen") &&
           FindVTable(a.vtAction, "_ZTVN12SelectScreen15ActionSelectionE") &&
           Find(a.mainMenuAddAllItems, "_ZN14MainMenuScreen11AddAllItemsEv") &&
           Find(a.onSettings, "_ZN14MainMenuScreen10OnSettingsEv") &&
           Find(a.onStartGame, "_ZN14MainMenuScreen11OnStartGameEv") && Find(a.textGet, "_ZN5CText3GetEPKc") &&
           Find(a.gameCtor, "_ZN10GameScreenC2Ev") && Find(a.displayCtor, "_ZN13DisplayScreenC2Ev") &&
           Find(a.audioCtor, "_ZN11AudioScreenC2Ev") && Find(a.controlsCtor, "_ZN14ControlsScreenC2Ev");
}

} // namespace

const Api *Resolve() {
    static Api api{};
    static const bool ok = ResolveInto(api);
    return ok ? &api : nullptr;
}

MobileMenu *Menu() { return Resolve() ? g_pMobileMenu : nullptr; }

void *TopScreen() {
    MobileMenu *menu = Menu();
    if (!menu || menu->screenStack.Count() == 0)
        return nullptr;
    return *menu->screenStack.Element(menu->screenStack.Count() - 1);
}

void AddRef(RwTexture *texture) { ++texture->refCount; }

} // namespace MenuGame
