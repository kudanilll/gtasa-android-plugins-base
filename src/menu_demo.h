#pragma once

namespace MenuDemo {
// Adds sample tiles and items to the game's menus: a scrolling list in Settings and the pause menu, a Multiplayer
// carousel next to Start Game, a Start Game tile and a Display tab row. Call once from ON_MOD_LOAD, after
// Menu::Init().
void Init(const char *modName);
} // namespace MenuDemo
