#pragma once

// Shared between the menu module's source files; not part of the public API.

#include "game_menu.h"
#include "menu.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Menu {

// The resolved game API once Init() succeeded, otherwise nullptr.
const MenuGame::Api *ActiveApi();

// Returns a key that this plugin's CText::Get hook turns back into `ascii`. Null or empty becomes "?".
const char *Text(const char *ascii);

struct Tile {
    const char *key = nullptr;
    std::string icon;
    RwTexture *fileTexture = nullptr; // Owned. Game textures are fetched again on every use, like the game does.
    Callback onPressed = nullptr;
    void *data = nullptr;
    Screen *opens = nullptr; // Takes precedence over onPressed
    int index = -1;
};

using TileList = std::vector<std::unique_ptr<Tile>>;

void AddTileTo(TileList &tiles, const char *title, const char *icon, Callback onPressed, void *data, Screen *opens,
               int index);

// Appends `tiles` to a FlowScreen's carousel, moving each to its `index` if `honorIndex`.
void InjectTiles(void *flowScreen, const TileList &tiles, bool honorIndex);

// GXT text of a key from Text(), or nullptr.
const std::uint16_t *Gxt(const char *key);

constexpr float Clamp01(float t) { return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t); }

// Value under a finger at `touchX` on a slider bar spanning x1..x2.
constexpr int SliderValue(float touchX, float x1, float x2, int min, int max) {
    if (x2 <= x1)
        return min;
    return min + static_cast<int>(Clamp01((touchX - x1) / (x2 - x1)) * static_cast<float>(max - min) + 0.5f);
}

// How full a slider bar is drawn, 0..1.
constexpr float SliderProgress(int value, int min, int max) {
    return max <= min ? 0.0f : Clamp01(static_cast<float>(value - min) / static_cast<float>(max - min));
}

// The option after `value`, wrapping around `count` values.
constexpr int NextOption(int value, int count) { return value + 1 < count ? value + 1 : 0; }

// Whether a finger at `x` takes hold of a slider whose bar spans x1..x2.
constexpr bool GrabsSlider(float x, float x1, float x2) { return x1 < x2 && x > x1 && x < x2; }

// The "Restore defaults" row each game tab ends with.
constexpr bool IsRestoreDefaults(const char *tag) { return tag && std::string_view(tag) == "MOB_RTD"; }

// Whether a tile's `index` is honoured in a game carousel. SAUtils finds its own Settings and Start Game tiles by
// position, so there ours always go last.
constexpr bool HonorsIndex(Location where) { return where == Location::MainMenu || where == Location::Pause; }

// Position for a tile with `index` among `count` tiles, itself appended last; Start Game is at `startGame` or -1.
constexpr int TilePosition(int index, int startGame, int count) {
    if (index == kAfterStartGame)
        return startGame >= 0 ? startGame + 1 : count - 1;
    return index >= 0 && index < count - 1 ? index : count - 1;
}

enum class ItemKind { Button, Option, Slider, Label };

struct ItemData final : Item {
    ItemKind kind = ItemKind::Label;
    const char *key = nullptr;      // title
    std::vector<const char *> text; // Option values, or the Label's text
    int value = 0;
    int min = 0;
    int max = 0;
    Callback onPressed = nullptr;
    Screen *opens = nullptr; // Buttons only; takes precedence over onPressed
    ValueCallback onChanged = nullptr;
    ValueFormatter format = nullptr;
    void *data = nullptr;
};

using ItemList = std::vector<std::unique_ptr<ItemData>>;

Item *AddButtonTo(ItemList &items, const char *title, Callback onPressed, void *data, Screen *opens);
Item *AddOptionTo(ItemList &items, const char *title, std::vector<const char *> values, int initial,
                  ValueCallback onChanged, void *data);
Item *AddSliderTo(ItemList &items, const char *title, int min, int max, int initial, ValueCallback onChanged,
                  void *data, ValueFormatter format);
Item *AddLabelTo(ItemList &items, const char *title, const char *text);

// Appends game-side selections for `items` to a SelectScreen. The game owns and frees them.
void InjectItems(void *selectScreen, const ItemList &items);

void InstallItemHooks(const MenuGame::Api &api);

struct ListData final : List {
    const char *key = nullptr; // title
    ItemList items;
};

struct CarouselData final : Carousel {
    TileList tiles;
};

} // namespace Menu
