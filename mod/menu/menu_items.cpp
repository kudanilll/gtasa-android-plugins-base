#include "menu_internal.h"
#include "menu_scroll.h"

#include <mod/amlmod.h>
#include <plugin-sdk/game_sa/engine/MobileMenu.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <new>

static_assert(Menu::SliderValue(0.0f, 100.0f, 200.0f, 0, 10) == 0);    // left of the bar
static_assert(Menu::SliderValue(300.0f, 100.0f, 200.0f, 0, 10) == 10); // right of the bar
static_assert(Menu::SliderValue(150.0f, 100.0f, 200.0f, 0, 10) == 5);  // middle
static_assert(Menu::SliderValue(150.0f, 100.0f, 100.0f, 3, 10) == 3);  // empty bar
static_assert(Menu::SliderValue(175.0f, 100.0f, 200.0f, -10, 10) == 5);
static_assert(Menu::SliderProgress(5, 0, 10) == 0.5f && Menu::SliderProgress(-1, 0, 10) == 0.0f &&
              Menu::SliderProgress(11, 0, 10) == 1.0f && Menu::SliderProgress(4, 4, 4) == 0.0f);
// Like SettingSelection::HandleInput, a slider only takes a finger that lands inside its bar.
static_assert(Menu::GrabsSlider(150.0f, 100.0f, 200.0f) && !Menu::GrabsSlider(50.0f, 100.0f, 200.0f) &&
              !Menu::GrabsSlider(100.0f, 100.0f, 200.0f) && !Menu::GrabsSlider(150.0f, 0.0f, 0.0f));
static_assert(Menu::IsRestoreDefaults("MOB_RTD") && !Menu::IsRestoreDefaults("FEP_OPT") &&
              !Menu::IsRestoreDefaults(nullptr));
static_assert(Menu::NextOption(0, 2) == 1 && Menu::NextOption(1, 2) == 0 && Menu::NextOption(0, 1) == 0 &&
              Menu::NextOption(5, 3) == 0);

// Where the game reads touch input (ActionSelection::HandleInput, FlowScreen::Update).
static_assert(offsetof(MobileMenu, m_vMousePos) == (sizeof(void *) == 8 ? 0x90 : 0x70));
static_assert(offsetof(MobileMenu, m_nPointerState) == (sizeof(void *) == 8 ? 0xB0 : 0x90));

namespace Menu {
namespace {

const MenuGame::Api *g_pApi = nullptr;
std::array<ItemList, 4> g_TabItems; // Indexed by Tab

// The game-side object: shaped like SelectScreen::ActionSelection, with our vtable.
struct ItemSelection : MenuGame::Selection {
    ItemData *item;
    float sliderX1; // Bar span from the last Render, used by touch input
    float sliderX2;
    bool grabbed; // A finger is dragging the slider
};

constexpr float kTitleShare = 0.6f; // MenuSelection::GetTextPercent; values are drawn right of this
constexpr int kAlignRight = 2;
constexpr int kTextStyle = 2;
constexpr int kHidSelect = 64;
constexpr int kAudioSelect = 1;

// Header words (offset-to-top, typeinfo) followed by the slots, copied from ActionSelection.
std::array<void *, 2 + MenuGame::kSelectionSlots> g_VTable{};

ItemSelection &Proxy(MenuGame::Selection *self) { return *static_cast<ItemSelection *>(self); }

void Change(ItemData &item, int value) {
    if (value == item.value)
        return;
    const int old = item.value;
    item.value = value;
    if (item.onChanged)
        item.onChanged(old, value, item.data);
}

void DrawValue(void *screen, const std::uint16_t *text, CVector2D pos, CVector2D size, float pad) {
    if (!text)
        return;
    const CRGBA color(240, 240, 240, 255);
    const float x = pos.x + size.x * kTitleShare;
    const float midY = pos.y + size.y * 0.5f;
    g_pApi->drawTextDirect(screen, text, kAlignRight, kTextStyle, &color, true, {x, midY - 8.0f - pad},
                           {size.x - size.x * kTitleShare, pad * 2.0f + 16.0f}, false, nullptr);
}

void DrawFormatted(void *screen, const char *ascii, CVector2D pos, CVector2D size, float pad) {
    if (!ascii)
        return;
    std::vector<std::uint16_t> gxt(std::strlen(ascii) + 1, 0);
    g_pApi->asciiToGxt(ascii, gxt.data());
    DrawValue(screen, gxt.data(), pos, size, pad);
}

float GetTextPercent(MenuGame::Selection *self) {
    const ItemData &item = *Proxy(self).item;
    const bool fullWidth = item.kind == ItemKind::Button || (item.kind == ItemKind::Label && item.text.empty());
    return fullWidth ? 1.0f : kTitleShare;
}

void Render(MenuGame::Selection *self, void *screen, CVector2D pos, CVector2D size, float pad) {
    g_pApi->selectionRender(self, screen, pos, size, pad); // Title, in the game's style
    ItemSelection &proxy = Proxy(self);
    const ItemData &item = *proxy.item;
    switch (item.kind) {
    case ItemKind::Option:
        if (item.value >= 0 && static_cast<std::size_t>(item.value) < item.text.size())
            DrawValue(screen, Gxt(item.text[item.value]), pos, size, pad);
        break;
    case ItemKind::Label:
        if (!item.text.empty())
            DrawValue(screen, Gxt(item.text[0]), pos, size, pad);
        break;
    case ItemKind::Slider: {
        proxy.sliderX1 = pos.x + size.x * kTitleShare;
        proxy.sliderX2 = pos.x + size.x;
        if (item.format) {
            DrawFormatted(screen, item.format(item.value, item.data), pos, size, pad);
        } else {
            const float midY = pos.y + size.y * 0.5f;
            g_pApi->drawSlider(screen, {proxy.sliderX1, midY - 9.0f}, {proxy.sliderX2, midY + 9.0f},
                               SliderProgress(item.value, item.min, item.max), true, 0xFF, false);
        }
        break;
    }
    case ItemKind::Button:
        break;
    }
}

void HandleInput(MenuGame::Selection *self, void *screen, float deltaTime) {
    ItemSelection &proxy = Proxy(self);
    ItemData &item = *proxy.item;
    MobileMenu *menu = MenuGame::Menu();
    const int device = *g_pApi->lastDevice;
    if (!menu || device < 0 || device > 3)
        return;
    const OSPointerState state = menu->m_nPointerState[device];
    // Same activation rule as ActionSelection: the select button, or the finger lifting.
    const bool released =
        (g_pApi->hidImplements(kHidSelect) && g_pApi->hidIsReleased(kHidSelect)) || state == OSPS_ButtonReleased;
    switch (item.kind) {
    case ItemKind::Button:
        if (released) {
            g_pApi->reportFrontendAudio(g_pApi->audioEngine, kAudioSelect, 0.0f, 1.0f);
            if (item.opens)
                Open(item.opens);
            else if (item.onPressed)
                item.onPressed(item.data);
        }
        break;
    case ItemKind::Option:
        if (released) {
            g_pApi->reportFrontendAudio(g_pApi->audioEngine, kAudioSelect, 0.0f, 1.0f);
            Change(item, NextOption(item.value, static_cast<int>(item.text.size())));
        }
        break;
    case ItemKind::Slider: {
        // Like SettingSelection: the press must land on the bar, then the value follows the finger until it lifts.
        // Touching the title or scrolling the list leaves the value alone.
        const float x = menu->m_vMousePos[device].x;
        if (state == OSPS_ButtonPressed)
            proxy.grabbed = GrabsSlider(x, proxy.sliderX1, proxy.sliderX2);
        else if (state != OSPS_ButtonDown || MenuGame::IsScrolling(screen))
            proxy.grabbed = false;
        if (proxy.grabbed)
            Change(item, SliderValue(x, proxy.sliderX1, proxy.sliderX2, item.min, item.max));
        break;
    }
    case ItemKind::Label:
        break;
    }
}

MenuGame::Selection *MakeSelection(ItemData *item) {
    auto *proxy = static_cast<ItemSelection *>(::operator new(sizeof(ItemSelection)));
    proxy->vtable = g_VTable.data() + 2;
    proxy->tag = item->key;
    proxy->item = item;
    proxy->sliderX1 = 0.0f;
    proxy->sliderX2 = 0.0f;
    proxy->grabbed = false;
    return proxy;
}

ItemData *NewItem(ItemList &items, ItemKind kind, const char *title) {
    items.push_back(std::make_unique<ItemData>());
    ItemData *item = items.back().get();
    item->kind = kind;
    item->key = Text(title);
    return item;
}

ItemList &TabItems(Tab tab) { return g_TabItems[static_cast<int>(tab)]; }

// Appends `items` to a game tab, keeping the tab's "Restore defaults" row last (and in its bottom slot).
void InjectIntoTab(void *tab, const ItemList &items) {
    auto &rows = MenuGame::SelectItems(tab);
    const unsigned int gameRows = rows.Count();
    InjectItems(tab, items);
    if (gameRows > 0 && IsRestoreDefaults(rows[gameRows - 1]->tag)) {
        MenuGame::Selection **first = rows.Element(0);
        std::rotate(first + gameRows - 1, first + gameRows, first + rows.Count());
    }
    EnableScrolling(tab);
}

// One hook per tab constructor: call the game's, then append this plugin's items. On ARM32 a constructor returns
// `this` and the game uses it, so the hook hands back the original's result.
template <Tab T> struct TabHook {
    static inline void *(*orig)(void *self) = nullptr;
    static void *Hooked(void *self) {
        void *result = orig(self);
        if (!TabItems(T).empty())
            InjectIntoTab(self, TabItems(T));
        return result;
    }
    static void Install(void *target) {
        aml->Hook(target, reinterpret_cast<void *>(&Hooked), reinterpret_cast<void **>(&orig));
    }
};

} // namespace

void InjectItems(void *selectScreen, const ItemList &items) {
    for (const auto &item : items)
        g_pApi->addItem(selectScreen, MakeSelection(item.get()));
}

void InstallItemHooks(const MenuGame::Api &api) {
    g_pApi = &api;
    std::memcpy(g_VTable.data(), api.vtAction - 2, sizeof(g_VTable));
    g_VTable[2 + MenuGame::kSelRender] = reinterpret_cast<void *>(&Render);
    g_VTable[2 + MenuGame::kSelTextPercent] = reinterpret_cast<void *>(&GetTextPercent);
    g_VTable[2 + MenuGame::kSelHandleInput] = reinterpret_cast<void *>(&HandleInput);

    TabHook<Tab::Game>::Install(api.gameCtor);
    TabHook<Tab::Display>::Install(api.displayCtor);
    TabHook<Tab::Audio>::Install(api.audioCtor);
    TabHook<Tab::Controls>::Install(api.controlsCtor);
}

int Item::Value() const { return static_cast<const ItemData *>(this)->value; }

void Item::SetValue(int value) {
    ItemData &item = *static_cast<ItemData *>(this);
    if (item.kind == ItemKind::Option)
        item.value = item.text.empty() ? 0 : std::max(0, std::min(value, static_cast<int>(item.text.size()) - 1));
    else if (item.kind == ItemKind::Slider)
        item.value = std::max(item.min, std::min(value, item.max));
    else
        item.value = value;
}

Item *AddButtonTo(ItemList &items, const char *title, Callback onPressed, void *data, Screen *opens) {
    ItemData *item = NewItem(items, ItemKind::Button, title);
    item->onPressed = onPressed;
    item->data = data;
    item->opens = opens;
    return item;
}

Item *AddOptionTo(ItemList &items, const char *title, std::vector<const char *> values, int initial,
                  ValueCallback onChanged, void *data) {
    ItemData *item = NewItem(items, ItemKind::Option, title);
    for (const char *value : values)
        item->text.push_back(Text(value));
    item->onChanged = onChanged;
    item->data = data;
    item->SetValue(initial);
    return item;
}

Item *AddSliderTo(ItemList &items, const char *title, int min, int max, int initial, ValueCallback onChanged,
                  void *data, ValueFormatter format) {
    ItemData *item = NewItem(items, ItemKind::Slider, title);
    item->min = min;
    item->max = max < min ? min : max;
    item->onChanged = onChanged;
    item->data = data;
    item->format = format;
    item->SetValue(initial);
    return item;
}

Item *AddLabelTo(ItemList &items, const char *title, const char *text) {
    ItemData *item = NewItem(items, ItemKind::Label, title);
    if (text)
        item->text.push_back(Text(text));
    return item;
}

Item *AddButton(Tab tab, const char *title, Callback onPressed, void *data) {
    return AddButtonTo(TabItems(tab), title, onPressed, data, nullptr);
}
Item *AddButton(Tab tab, const char *title, Screen *opens) {
    return AddButtonTo(TabItems(tab), title, nullptr, nullptr, opens);
}
Item *AddOption(Tab tab, const char *title, std::vector<const char *> values, int initial, ValueCallback onChanged,
                void *data) {
    return AddOptionTo(TabItems(tab), title, std::move(values), initial, onChanged, data);
}
Item *AddSlider(Tab tab, const char *title, int min, int max, int initial, ValueCallback onChanged, void *data,
                ValueFormatter format) {
    return AddSliderTo(TabItems(tab), title, min, max, initial, onChanged, data, format);
}
Item *AddLabel(Tab tab, const char *title, const char *text) { return AddLabelTo(TabItems(tab), title, text); }

} // namespace Menu
