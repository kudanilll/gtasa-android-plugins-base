#include <mod/amlmod.h>
#include <mod/logger.h>

#include <plugin-sdk/game_sa/Events.h>
#include <plugin-sdk/game_sa/base/Timer.h>
#include <plugin-sdk/game_sa/engine/Font.h>
#include <plugin-sdk/game_sa/engine/Sprite2d.h>
#include <plugin-sdk/game_sa/entity/PlayerPed.h>

MYMOD(net.rusjj.baseplugin, BasePlugin, 1.0, RusJJ)

static bool g_bMessageTriggered = false;
static uint32_t g_nTimeToHide = 0;

ON_MOD_LOAD() {
    logger->SetTag("BasePlugin");
    logger->Info("BasePlugin loaded");

    Events::drawHudEvent += []() {
        CPlayerPed *player = FindPlayerPed(-1);
        if (!player)
            return;

        uint32_t currentTime = CTimer::GetTimeMS();

        // first spawn in-game starts the 3s window
        if (!g_bMessageTriggered) {
            g_bMessageTriggered = true;
            g_nTimeToHide = currentTime + 3000;
        }

        if (currentTime <= g_nTimeToHide) {
            CSprite2d::DrawRect(CRect(40.0f, 40.0f, 260.0f, 95.0f), CRGBA(0, 0, 0, 180));

            CFont::SetScale(0.75f, 1.4f);
            CFont::SetOrientation(ALIGN_LEFT);
            CFont::SetFontStyle(FO_FONT_STYLE_STANDARD);
            CFont::SetProportional(true);
            CFont::SetColor(CRGBA(255, 255, 255, 255));
            CFont::SetDropColor(CRGBA(0, 0, 0, 255));
            CFont::SetEdge(1);
            CFont::PrintString(55.0f, 52.0f, "mod loaded!");
            CFont::RenderFontBuffer();
        }
    };
}
