#include "menu_scroll.h"

#include "menu_internal.h"

#include <plugin-sdk/game_sa/engine/MobileMenu.h>

#include <array>
#include <cstring>
#include <memory>
#include <unordered_map>

// The game's list area is 70..430 with 9 rows of 40.
static_assert(Menu::RowHeight(360.0f) == 40.0f);
static_assert(Menu::MinScroll(360.0f, 0) == 0.0f && Menu::MinScroll(360.0f, 9) == 0.0f);
static_assert(Menu::MinScroll(360.0f, 20) == -440.0f); // 11 rows past the bottom
static_assert(Menu::ClampScroll(10.0f, 360.0f, 20) == 0.0f && Menu::ClampScroll(-500.0f, 360.0f, 20) == -440.0f &&
              Menu::ClampScroll(-100.0f, 360.0f, 20) == -100.0f && Menu::ClampScroll(-100.0f, 360.0f, 5) == 0.0f);
// A drag starts once the finger travels half a row, like KeyboardControlsScreen::IsScrolling.
static_assert(Menu::IsDrag(100.0f, 121.0f, 40.0f) && Menu::IsDrag(100.0f, 79.0f, 40.0f));
static_assert(!Menu::IsDrag(100.0f, 115.0f, 40.0f) && !Menu::IsDrag(0.0f, 300.0f, 40.0f));
// Once a gesture has dragged, it stays a drag even if the finger comes back near where it started.
static_assert(Menu::Dragged(false, 100.0f, 121.0f, 40.0f) && Menu::Dragged(true, 100.0f, 101.0f, 40.0f));
static_assert(!Menu::Dragged(false, 100.0f, 101.0f, 40.0f));

namespace Menu {
namespace {

using namespace MenuGame;

using VTableCopy = std::array<void *, 2 + kScreenSlots>; // Header words, then the slots

struct ScrollState {
    void **original = nullptr; // The screen's vtable before ours
    float scrollY = 0.0f;
    float dragStartY = 0.0f; // 0 while no finger went down on the list
    float lastY = 0.0f;
    bool dragged = false; // This gesture has scrolled; its release is not a tap
};

// CRect as the game returns it from GetScissorRect: pixels, y growing downwards.
struct ScissorRect {
    float left;
    float bottom;
    float right;
    float top;
};

std::unordered_map<void **, std::unique_ptr<VTableCopy>> g_VTables; // Original vtable -> our copy
std::unordered_map<void *, ScrollState> g_States;                   // Screen -> state

ScrollState *State(void *screen) {
    const auto it = g_States.find(screen);
    return it != g_States.end() ? &it->second : nullptr;
}

float StartY(void *screen) { return Slot<float (*)(void *)>(screen, kScreenGetStartY)(screen); }
float EndY(void *screen) { return Slot<float (*)(void *)>(screen, kScreenGetEndY)(screen); }

// The finger the menu is following, or nullptr.
MobileMenu *Pointer(int &device) {
    MobileMenu *menu = MenuGame::Menu();
    device = *ActiveApi()->lastDevice;
    return menu && device >= 0 && device <= 3 ? menu : nullptr;
}

// Per frame, before the game's Update: follow the finger like KeyboardControlsScreen::ManageScrolling.
void ManageScrolling(void *screen, ScrollState &state) {
    int device = 0;
    MobileMenu *menu = Pointer(device);
    if (!menu)
        return;
    const float start = StartY(screen);
    const float area = EndY(screen) - start;
    const float y = menu->m_vMousePos[device].y;
    OSPointerState &pointer = menu->m_nPointerState[device];
    switch (pointer) {
    case OSPS_ButtonPressed:
        state.dragStartY = y >= start && y <= start + area ? y : 0.0f;
        state.dragged = false;
        break;
    case OSPS_ButtonUp:
        state.dragStartY = 0.0f;
        state.dragged = false;
        break;
    case OSPS_ButtonDown:
        if (state.dragStartY != 0.0f)
            state.scrollY += y - state.lastY;
        state.dragged = Dragged(state.dragged, state.dragStartY, y, RowHeight(area));
        break;
    case OSPS_ButtonReleased:
        // The lift that ends a drag must not tap the row under the finger; the game swallows a release the same
        // way when the selection changes.
        state.dragged = Dragged(state.dragged, state.dragStartY, y, RowHeight(area));
        if (state.dragged)
            pointer = OSPS_ButtonUp;
        break;
    default:
        break;
    }
    state.scrollY = ClampScroll(state.scrollY, area, static_cast<int>(SelectItems(screen).Count()));
    state.lastY = y;
}

void *Dtor(void *self) {
    ScrollState *state = State(self);
    void **original = state ? state->original : nullptr;
    g_States.erase(self);
    return original ? reinterpret_cast<void *(*)(void *)>(original[kScreenDtor])(self) : self;
}

void DeletingDtor(void *self) {
    ScrollState *state = State(self);
    void **original = state ? state->original : nullptr;
    g_States.erase(self);
    if (original)
        reinterpret_cast<void (*)(void *)>(original[kScreenDeletingDtor])(self);
}

void Update(void *self, float deltaTime) {
    ScrollState *state = State(self);
    if (!state)
        return;
    ManageScrolling(self, *state);
    reinterpret_cast<void (*)(void *, float)>(state->original[kScreenUpdate])(self, deltaTime);
}

float GetScrollY(void *self) {
    const ScrollState *state = State(self);
    return state ? state->scrollY : 0.0f;
}

float GetYSize(void *self, int index) { return RowHeight(EndY(self) - StartY(self)); }

ScissorRect GetScissorRect(void *self) {
    const float toPixels = static_cast<float>(ActiveApi()->screenHeight()) / 480.0f;
    return {0.0f, EndY(self) * toPixels, static_cast<float>(ActiveApi()->screenWidth()), StartY(self) * toPixels};
}

bool IsScrolling(void *self) {
    const ScrollState *state = State(self);
    int device = 0;
    MobileMenu *menu = Pointer(device);
    if (!state || !menu)
        return false;
    return Dragged(state->dragged, state->dragStartY, menu->m_vMousePos[device].y, GetYSize(self, 0));
}

VTableCopy &ScrollingVTable(void **original) {
    std::unique_ptr<VTableCopy> &copy = g_VTables[original];
    if (!copy) {
        copy = std::make_unique<VTableCopy>();
        std::memcpy(copy->data(), original - 2, sizeof(VTableCopy));
        void **slots = copy->data() + 2;
        slots[kScreenDtor] = reinterpret_cast<void *>(&Dtor);
        slots[kScreenDeletingDtor] = reinterpret_cast<void *>(&DeletingDtor);
        slots[kScreenUpdate] = reinterpret_cast<void *>(&Update);
        slots[kScreenGetScrollY] = reinterpret_cast<void *>(&GetScrollY);
        slots[kScreenGetYSize] = reinterpret_cast<void *>(&GetYSize);
        slots[kScreenGetScissorRect] = reinterpret_cast<void *>(&GetScissorRect);
        slots[kScreenIsScrolling] = reinterpret_cast<void *>(&IsScrolling);
    }
    return *copy;
}

} // namespace

void EnableScrolling(void *selectScreen) {
    if (!ActiveApi() || State(selectScreen))
        return;
    void **original = VTable(selectScreen);
    ScrollState state;
    state.original = original;
    g_States.emplace(selectScreen, state);
    VTable(selectScreen) = ScrollingVTable(original).data() + 2;
}

} // namespace Menu
