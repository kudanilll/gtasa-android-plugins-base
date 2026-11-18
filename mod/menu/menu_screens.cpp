#include "menu_internal.h"
#include "menu_scroll.h"

#include <mod/logger.h>

#include <cstring>
#include <new>

namespace Menu {
namespace {

std::vector<std::unique_ptr<ListData>> g_Lists;
std::vector<std::unique_ptr<CarouselData>> g_Carousels;

ListData &Data(List *list) { return static_cast<ListData &>(*list); }
CarouselData &Data(Carousel *carousel) { return static_cast<CarouselData &>(*carousel); }

// Zeroed storage for a game screen; the game frees it with operator delete when the screen closes.
void *NewGameScreen(std::size_t size) { return std::memset(::operator new(size), 0, size); }

} // namespace

List *NewList(const char *title) {
    g_Lists.push_back(std::make_unique<ListData>());
    g_Lists.back()->key = Text(title);
    return g_Lists.back().get();
}

Carousel *NewCarousel() {
    g_Carousels.push_back(std::make_unique<CarouselData>());
    return g_Carousels.back().get();
}

void Open(Screen *screen) {
    const MenuGame::Api *api = ActiveApi();
    if (!api || !screen)
        return;
    if (!MenuGame::TopScreen()) {
        logger->Error("Menu: Open() needs the game's menu to be showing");
        return;
    }
    if (screen->m_bCarousel) {
        CarouselData &carousel = Data(static_cast<Carousel *>(screen));
        if (carousel.tiles.empty()) {
            logger->Error("Menu: not opening a carousel without tiles");
            return;
        }
        void *game = NewGameScreen(MenuGame::kFlowScreenSize);
        api->flowCtor(game, true, false); // Same arguments as SettingsScreen
        InjectTiles(game, carousel.tiles, true);
        api->pushScreen(game, true);
    } else {
        ListData &list = Data(static_cast<List *>(screen));
        void *game = NewGameScreen(MenuGame::kListScreenSize);
        api->charSelectCtor(game, list.key, true);                       // Same arguments as the game's own tabs
        MenuGame::At<bool>(game, MenuGame::kSelectLastAtBottom) = false; // Keep rows in the order they were added
        InjectItems(game, list.items);
        EnableScrolling(game);
        api->pushScreen(game, true);
    }
}

Item *List::AddButton(const char *title, Callback onPressed, void *data) {
    return AddButtonTo(Data(this).items, title, onPressed, data, nullptr);
}
Item *List::AddButton(const char *title, Screen *opens) {
    return AddButtonTo(Data(this).items, title, nullptr, nullptr, opens);
}
Item *List::AddOption(const char *title, std::vector<const char *> values, int initial, ValueCallback onChanged,
                      void *data) {
    return AddOptionTo(Data(this).items, title, std::move(values), initial, onChanged, data);
}
Item *List::AddSlider(const char *title, int min, int max, int initial, ValueCallback onChanged, void *data,
                      ValueFormatter format) {
    return AddSliderTo(Data(this).items, title, min, max, initial, onChanged, data, format);
}
Item *List::AddLabel(const char *title, const char *text) { return AddLabelTo(Data(this).items, title, text); }

void Carousel::AddTile(const char *title, const char *icon, Callback onPressed, void *data, int index) {
    AddTileTo(Data(this).tiles, title, icon, onPressed, data, nullptr, index);
}
void Carousel::AddTile(const char *title, const char *icon, Screen *opens, int index) {
    AddTileTo(Data(this).tiles, title, icon, nullptr, nullptr, opens, index);
}

} // namespace Menu
