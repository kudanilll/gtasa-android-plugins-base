#pragma once

// Native GTA SA frontend menus for this plugin: tiles in the game's carousels, list screens and items, drawn by the
// game itself. Every plugin built from the template carries its own copy, so several can run side by side (and
// next to SAUtils). Text is plain ASCII. All calls are for the game thread.

#include <vector>

namespace Menu {

enum class Location { MainMenu, Pause, StartGame, Settings };

using Callback = void (*)(void *data);

// Installs the hooks. Call once from ON_MOD_LOAD, before the menus are built. On an unsupported game build it logs
// the missing symbol and every other call in this header does nothing.
void Init();

// For `index`: right after the game's Start Game tile, wherever it is (a Resume tile only shows with a save). Each
// such tile goes straight after Start Game, so add several in reverse order.
inline constexpr int kAfterStartGame = -2;

// Adds a tile to one of the game's carousels. `icon` is a .png/.jpg/.jpeg path or the name of a texture already in
// the game (for example "menu_mainsettings"). `index` places the tile among the others in the main and pause menus:
// a position, kAfterStartGame, or -1 for last. In Settings and Start Game tiles always go last, because SAUtils
// finds its tiles there by position.
void AddTile(Location where, const char *title, const char *icon, Callback onPressed, void *data = nullptr,
             int index = -1);

// The game's own Settings tabs that items can be added to.
enum class Tab { Game, Display, Audio, Controls };

using ValueCallback = void (*)(int oldValue, int newValue, void *data);
// Turns a slider value into the text drawn instead of the bar, for example "75%".
using ValueFormatter = const char *(*)(int value, void *data);

// A list item. Owned by the menu module and valid for the plugin's lifetime.
class Item {
  public:
    int Value() const;
    // Sets the value without calling the item's callback. Clamped to the item's range.
    void SetValue(int value);

  protected:
    Item() = default;
};

Item *AddButton(Tab tab, const char *title, Callback onPressed, void *data = nullptr);
// Cycles through `values` on each tap. The value is the index into `values`.
Item *AddOption(Tab tab, const char *title, std::vector<const char *> values, int initial = 0,
                ValueCallback onChanged = nullptr, void *data = nullptr);
Item *AddSlider(Tab tab, const char *title, int min, int max, int initial, ValueCallback onChanged = nullptr,
                void *data = nullptr, ValueFormatter format = nullptr);
// A row that only shows text. `text`, if any, is drawn on the right.
Item *AddLabel(Tab tab, const char *title, const char *text = nullptr);

// A screen of this plugin's own: a List of items or a Carousel of tiles. Each describes the screen; the game
// object is built again every time it opens. Owned by the menu module and valid for the plugin's lifetime.
class Screen {
  protected:
    explicit Screen(bool carousel) : m_bCarousel(carousel) {}

  private:
    friend void Open(Screen *screen);
    bool m_bCarousel;
};

// A list like the game's Display tab. It scrolls once it has more rows than fit.
class List : public Screen {
  public:
    Item *AddButton(const char *title, Callback onPressed, void *data = nullptr);
    Item *AddButton(const char *title, Screen *opens);
    Item *AddOption(const char *title, std::vector<const char *> values, int initial = 0,
                    ValueCallback onChanged = nullptr, void *data = nullptr);
    Item *AddSlider(const char *title, int min, int max, int initial, ValueCallback onChanged = nullptr,
                    void *data = nullptr, ValueFormatter format = nullptr);
    Item *AddLabel(const char *title, const char *text = nullptr);

  protected:
    List() : Screen(false) {}
};

// A row of tiles like the game's Settings screen.
class Carousel : public Screen {
  public:
    void AddTile(const char *title, const char *icon, Callback onPressed, void *data = nullptr, int index = -1);
    void AddTile(const char *title, const char *icon, Screen *opens, int index = -1);

  protected:
    Carousel() : Screen(true) {}
};

List *NewList(const char *title);
Carousel *NewCarousel();

// Opens a screen on top of the current menu, as if a tile leading to it was pressed. Does nothing (and logs) while
// the game's menu is not showing.
void Open(Screen *screen);

void AddTile(Location where, const char *title, const char *icon, Screen *opens, int index = -1);
Item *AddButton(Tab tab, const char *title, Screen *opens);

} // namespace Menu
