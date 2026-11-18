#include "menu_demo.h"

#include <mod/logger.h>
#include <mod/menu/menu.h>

#include <cstdio>

namespace MenuDemo {
namespace {

const char *Percent(int value, void *) {
    static char text[8];
    std::snprintf(text, sizeof(text), "%d%%", value);
    return text;
}

} // namespace

void Init(const char *modName) {
    Menu::List *about = Menu::NewList("About");
    about->AddLabel("Built with", "AML mod template");

    // Twelve rows, so the list scrolls.
    Menu::List *list = Menu::NewList(modName);
    list->AddButton("Say Hello", [](void *) { logger->Info("Hello from the menu"); });
    list->AddButton("About", about);
    list->AddOption("Text Box", {"Off", "On"}, 1, [](int, int value, void *) { logger->Info("Text Box: %d", value); });
    list->AddOption("Difficulty", {"Easy", "Normal", "Hard"}, 1,
                    [](int, int value, void *) { logger->Info("Difficulty: %d", value); });
    list->AddSlider(
        "Volume", 0, 100, 80, [](int, int value, void *) { logger->Info("Volume: %d", value); }, nullptr, Percent);
    list->AddSlider("Brightness", 0, 10, 5, [](int, int value, void *) { logger->Info("Brightness: %d", value); });
    for (const char *row : {"Row 1", "Row 2", "Row 3", "Row 4", "Row 5", "Row 6"})
        list->AddLabel(row, "label");

    // The same list from Settings and from the pause menu.
    Menu::AddTile(Menu::Location::Settings, modName, "menu_mainsettings", list);
    Menu::AddTile(Menu::Location::Pause, modName, "menu_mainsettings", list);

    // A second game mode next to Start Game: a carousel of its own that leads to more lists.
    Menu::List *servers = Menu::NewList("Servers");
    for (const char *server : {"Los Santos RP", "San Fierro DM", "Las Venturas Race"})
        servers->AddButton(server, [](void *) { logger->Info("Connect pressed"); });

    Menu::List *player = Menu::NewList("Player");
    player->AddLabel("Name", "Player");
    player->AddOption("Skin", {"CJ", "Sweet", "Ryder"});

    Menu::Carousel *multiplayer = Menu::NewCarousel();
    multiplayer->AddTile("Servers", "menu_mainplay", servers);
    multiplayer->AddTile("Player", "menu_mainstats", player);
    Menu::AddTile(Menu::Location::MainMenu, "Multiplayer", "menu_mainplay", multiplayer, Menu::kAfterStartGame);

    // A tile that only runs code, in the Start Game menu.
    Menu::AddTile(Menu::Location::StartGame, "Quick Start", "menu_mainresume",
                  [](void *) { logger->Info("Quick Start pressed"); });

    // A row in the game's own Display tab.
    Menu::AddOption(Menu::Tab::Display, "Mod HUD", {"Off", "On"}, 1,
                    [](int, int value, void *) { logger->Info("Mod HUD: %d", value); });
}

} // namespace MenuDemo
