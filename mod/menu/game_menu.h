#pragma once

// The only place the menu module touches GTA SA's frontend: object layouts and resolved functions for
// v2.00 (ARM32) and v2.10 (ARM64). Values come from the v2.00 decompile and the v2.10 disassembly.

#include <plugin-sdk/game_sa/base/Lists.h>
#include <plugin-sdk/gta_base/RGBA.h>
#include <plugin-sdk/gta_base/Vector.h>

#include <cstddef>
#include <cstdint>

struct RwTexture;
struct MobileMenu;

namespace MenuGame {

#if defined(__aarch64__)
inline constexpr std::size_t kFlowIsPause = 0x15;
inline constexpr std::size_t kFlowSelected = 0x48;
inline constexpr std::size_t kFlowItems = 0x58;
inline constexpr std::size_t kSelectItems = 0x20;
inline constexpr std::size_t kSelectLastAtBottom = 0x44; // bool: a short list draws its last row at the bottom
inline constexpr std::size_t kTextureRefCount = 0x64;
inline constexpr std::size_t kListScreenSize = 0x60; // DisplayScreen, a CharSelectScreen
inline constexpr std::size_t kFlowScreenSize = 0x78; // StartGameScreen, a FlowScreen
#else
inline constexpr std::size_t kFlowIsPause = 0x0D;
inline constexpr std::size_t kFlowSelected = 0x30;
inline constexpr std::size_t kFlowItems = 0x3C;
inline constexpr std::size_t kSelectItems = 0x14;
inline constexpr std::size_t kSelectLastAtBottom = 0x30;
inline constexpr std::size_t kTextureRefCount = 0x54;
inline constexpr std::size_t kListScreenSize = 0x54;
inline constexpr std::size_t kFlowScreenSize = 0x58;
#endif

// Vtable slots, counted from the address object vptrs point at. The order is the same on both ABIs.
inline constexpr int kScreenDtor = 0;
inline constexpr int kScreenDeletingDtor = 1;
inline constexpr int kScreenUpdate = 2;
inline constexpr int kScreenGetScrollY = 15;
inline constexpr int kScreenGetStartY = 16;
inline constexpr int kScreenGetYSize = 17;
inline constexpr int kScreenGetEndY = 18;
inline constexpr int kScreenGetScissorRect = 19;
inline constexpr int kScreenIsScrolling = 20;
inline constexpr int kScreenSlots = 21;

inline constexpr int kSelRender = 2;
inline constexpr int kSelTextPercent = 3;
inline constexpr int kSelHandleInput = 6;
inline constexpr int kSelectionSlots = 9;

// One tile of a FlowScreen carousel. The game calls onPressed with no usable arguments.
struct FlowItem {
    RwTexture *texture;
    const char *key; // GXT key, looked up through CText::Get
    void (*onPressed)();
};

// Common head of SelectScreen::MenuSelection and its subclasses.
struct Selection {
    void **vtable;
    const char *tag; // GXT key of the title
};

template <class T> T &At(void *object, std::size_t offset) {
    return *reinterpret_cast<T *>(static_cast<unsigned char *>(object) + offset);
}

inline OSArray<FlowItem> &FlowItems(void *flowScreen) { return At<OSArray<FlowItem>>(flowScreen, kFlowItems); }
inline int FlowSelected(void *flowScreen) { return At<int>(flowScreen, kFlowSelected); }
inline bool FlowIsPause(void *flowScreen) { return At<bool>(flowScreen, kFlowIsPause); }
inline OSArray<Selection *> &SelectItems(void *selectScreen) {
    return At<OSArray<Selection *>>(selectScreen, kSelectItems);
}
inline void **&VTable(void *object) { return *static_cast<void ***>(object); }

// A virtual function of `object`, as a plain function taking the object first.
template <class Fn> Fn Slot(void *object, int slot) { return reinterpret_cast<Fn>(VTable(object)[slot]); }

inline bool IsScrolling(void *selectScreen) {
    return Slot<bool (*)(void *)>(selectScreen, kScreenIsScrolling)(selectScreen);
}

struct Api {
    // Calls
    std::uint16_t *(*asciiToGxt)(const char *ascii, std::uint16_t *out);
    RwTexture *(*getTexture)(const char *name);
    void (*addItem)(void *selectScreen, Selection *item);
    void (*selectionRender)(Selection *self, void *screen, CVector2D pos, CVector2D size, float pad);
    // The game passes CRGBA by reference: it is not trivially copyable there.
    CVector2D (*drawTextDirect)(void *screen, const std::uint16_t *text, int align, int style, const CRGBA *color,
                                bool shadow, CVector2D pos, CVector2D size, bool wrap, int *lines);
    void (*drawSlider)(void *screen, CVector2D ul, CVector2D lr, float progress, bool active, std::uint8_t alpha,
                       bool flag);
    void (*charSelectCtor)(void *screen, const char *titleKey, bool back);
    void (*flowCtor)(void *screen, bool back, bool pause);
    void (*pushScreen)(void *screen, bool immediate);
    void (*reportFrontendAudio)(void *engine, int event, float volume, float speed);
    bool (*hidImplements)(int mapping);
    bool (*hidIsReleased)(int mapping);
    unsigned int (*screenWidth)();
    unsigned int (*screenHeight)();
    void *audioEngine;
    int *lastDevice;
    void **vtCharSelect; // slot 0 of each vtable
    void **vtStartGame;
    void **vtSettings;
    void **vtAction;
    // Hook targets
    void *mainMenuAddAllItems;
    void *onSettings;
    void *onStartGame;
    void *textGet;
    void *gameCtor;
    void *displayCtor;
    void *audioCtor;
    void *controlsCtor;
};

// Resolves everything once. Returns nullptr, after logging the first missing symbol, on an unsupported game.
const Api *Resolve();

// The game's gMobileMenu, or nullptr when Resolve() failed.
MobileMenu *Menu();

// Top of gMobileMenu's screen stack, or nullptr.
void *TopScreen();

void AddRef(RwTexture *texture);

} // namespace MenuGame
