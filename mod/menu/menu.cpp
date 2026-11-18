#include "menu_internal.h"

#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/texture_loader.h>
#include <plugin-sdk/game_sa/Events.h>
#include <plugin-sdk/game_sa/engine/MobileMenu.h>
#include <plugin-sdk/renderware/RwTexture.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string_view>
#include <unordered_map>

namespace Menu {
namespace {

const MenuGame::Api *g_pApi = nullptr;

std::deque<std::string> g_Texts;                                        // Stable key storage
std::unordered_map<const char *, std::vector<std::uint16_t>> g_GxtText; // key -> GXT string

std::array<TileList, 4> g_Tiles; // Indexed by Location

std::uint16_t *(*orig_TextGet)(void *self, const char *key) = nullptr;
std::uintptr_t (*orig_MainMenuAddAllItems)(void *self) = nullptr;
std::uintptr_t (*orig_OnSettings)(void *self) = nullptr;
std::uintptr_t (*orig_OnStartGame)(void *self) = nullptr;

std::uint16_t *Hooked_TextGet(void *self, const char *key) {
    const auto it = g_GxtText.find(key);
    return it != g_GxtText.end() ? it->second.data() : orig_TextGet(self, key);
}

constexpr bool EqualsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const char c = a[i] >= 'A' && a[i] <= 'Z' ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
        if (c != b[i])
            return false;
    }
    return true;
}

// True for paths ending in .png, .jpg or .jpeg (any case); anything else names a game texture.
constexpr bool IsImagePath(std::string_view path) {
    const auto dot = path.rfind('.');
    if (dot == std::string_view::npos)
        return false;
    const std::string_view ext = path.substr(dot + 1);
    return EqualsNoCase(ext, "png") || EqualsNoCase(ext, "jpg") || EqualsNoCase(ext, "jpeg");
}

static_assert(IsImagePath("/sdcard/samp.png") && IsImagePath("ICON.JPG") && IsImagePath("a.Jpeg"));
static_assert(!IsImagePath("menu_mainsettings") && !IsImagePath("png") && !IsImagePath("a.png.txt") &&
              !IsImagePath(""));

// SAUtils finds its own Settings and Start Game tiles by position, so ours never move in front of them there.
static_assert(HonorsIndex(Location::MainMenu) && HonorsIndex(Location::Pause));
static_assert(!HonorsIndex(Location::Settings) && !HonorsIndex(Location::StartGame));

// Where a new tile lands among `count` tiles (itself last), with Start Game at `startGame` (-1 if absent).
static_assert(TilePosition(kAfterStartGame, 1, 5) == 2 && TilePosition(kAfterStartGame, 0, 4) == 1);
static_assert(TilePosition(kAfterStartGame, -1, 5) == 4); // no Start Game: last
static_assert(TilePosition(1, -1, 5) == 1 && TilePosition(-1, 1, 5) == 4 && TilePosition(9, 1, 5) == 4);

RwTexture *TileTexture(Tile &tile) {
    if (IsImagePath(tile.icon)) {
        if (!tile.fileTexture) {
            tile.fileTexture = TextureLoader::LoadTextureFromFile(tile.icon.c_str());
            if (!tile.fileTexture) {
                logger->Error("Menu: cannot load icon %s, using menu_mainsettings", tile.icon.c_str());
                tile.icon = "menu_mainsettings"; // Do not retry the file every time the menu opens.
            }
        }
        if (tile.fileTexture)
            return tile.fileTexture;
    }
    RwTexture *texture = g_pApi->getTexture(tile.icon.c_str());
    return texture ? texture : g_pApi->getTexture("menu_mainsettings");
}

std::unordered_map<const char *, Tile *> g_TileByKey; // Every tile of this plugin, game carousels and our own

Tile *FindTile(const char *key) {
    const auto it = g_TileByKey.find(key);
    return it != g_TileByKey.end() ? it->second : nullptr;
}

// Position of the game's Start Game tile, or -1.
int StartGamePosition(OSArray<MenuGame::FlowItem> &items) {
    for (unsigned int i = 0; i < items.Count(); ++i)
        if (items[i].key && std::strcmp(items[i].key, "FEP_STG") == 0)
            return static_cast<int>(i);
    return -1;
}

// Every tile of this plugin shares this callback. The game calls it from the top screen's Update with the pressed
// tile selected, so the selected item's key tells which tile it was.
void Dispatch() {
    void *screen = MenuGame::TopScreen();
    if (!screen)
        return;
    auto &items = MenuGame::FlowItems(screen);
    const int selected = MenuGame::FlowSelected(screen);
    if (selected < 0 || static_cast<unsigned int>(selected) >= items.Count())
        return;
    Tile *tile = FindTile(items[selected].key);
    if (!tile)
        return;
    if (tile->opens)
        Open(tile->opens);
    else if (tile->onPressed)
        tile->onPressed(tile->data);
}

void InjectAt(void *flowScreen, Location where) {
    InjectTiles(flowScreen, g_Tiles[static_cast<int>(where)], HonorsIndex(where));
}

// Hooks return whatever the original left in the return register: on ARM32 the game uses a constructor's result
// as `this`, and other callers may read r0 too.
std::uintptr_t Hooked_MainMenuAddAllItems(void *self) {
    const std::uintptr_t result = orig_MainMenuAddAllItems(self);
    InjectAt(self, MenuGame::FlowIsPause(self) ? Location::Pause : Location::MainMenu);
    return result;
}

// After a callback that pushes a screen: fills that screen if it is the one `vtable` names. Running after the
// callback puts our tiles behind those SAUtils adds while the screen is built.
void InjectIntoPushed(void **vtable, Location where) {
    MobileMenu *menu = MenuGame::Menu();
    for (void *screen : {static_cast<void *>(menu->pendingScreen), MenuGame::TopScreen()}) {
        if (screen && MenuGame::VTable(screen) == vtable) {
            InjectAt(screen, where);
            return;
        }
    }
}

std::uintptr_t Hooked_OnSettings(void *self) {
    const std::uintptr_t result = orig_OnSettings(self);
    InjectIntoPushed(g_pApi->vtSettings, Location::Settings);
    return result;
}

// OnStartGame either pushes a StartGameScreen or starts a new game straight away.
std::uintptr_t Hooked_OnStartGame(void *self) {
    const std::uintptr_t result = orig_OnStartGame(self);
    InjectIntoPushed(g_pApi->vtStartGame, Location::StartGame);
    return result;
}

void ReleaseTextures() {
    for (auto &[key, tile] : g_TileByKey)
        if (tile->fileTexture) {
            RwTextureDestroy(tile->fileTexture);
            tile->fileTexture = nullptr;
        }
}

template <class Fn> void HookOrig(void *target, Fn hook, Fn &orig) {
    aml->Hook(target, reinterpret_cast<void *>(hook), reinterpret_cast<void **>(&orig));
}

} // namespace

const MenuGame::Api *ActiveApi() { return g_pApi; }

const char *Text(const char *ascii) {
    const std::string &text = g_Texts.emplace_back(ascii && *ascii ? ascii : "?");
    std::vector<std::uint16_t> gxt(text.size() + 1, 0);
    if (g_pApi)
        g_pApi->asciiToGxt(text.c_str(), gxt.data());
    g_GxtText.emplace(text.c_str(), std::move(gxt));
    return text.c_str();
}

const std::uint16_t *Gxt(const char *key) {
    const auto it = g_GxtText.find(key);
    return it != g_GxtText.end() ? it->second.data() : nullptr;
}

void InjectTiles(void *flowScreen, const TileList &tiles, bool honorIndex) {
    auto &items = MenuGame::FlowItems(flowScreen);
    for (const auto &tile : tiles) {
        RwTexture *texture = TileTexture(*tile);
        if (!texture)
            continue;
        MenuGame::AddRef(texture); // The screen releases one reference per item when it is destroyed.
        *items.AllocNew() = {texture, tile->key, &Dispatch};
        const int count = static_cast<int>(items.Count());
        const int position = honorIndex ? TilePosition(tile->index, StartGamePosition(items), count) : count - 1;
        MenuGame::FlowItem *first = items.Element(0);
        std::rotate(first + position, first + count - 1, first + count);
    }
}

void Init() {
    if (g_pApi)
        return;
    g_pApi = MenuGame::Resolve();
    if (!g_pApi)
        return;
    // Keys registered before Init have no GXT text yet.
    for (auto &[key, gxt] : g_GxtText)
        g_pApi->asciiToGxt(key, gxt.data());

    HookOrig(g_pApi->textGet, &Hooked_TextGet, orig_TextGet);
    HookOrig(g_pApi->mainMenuAddAllItems, &Hooked_MainMenuAddAllItems, orig_MainMenuAddAllItems);
    HookOrig(g_pApi->onSettings, &Hooked_OnSettings, orig_OnSettings);
    HookOrig(g_pApi->onStartGame, &Hooked_OnStartGame, orig_OnStartGame);
    InstallItemHooks(*g_pApi);
    Events::shutdownRwEvent += ReleaseTextures;
}

void AddTileTo(TileList &tiles, const char *title, const char *icon, Callback onPressed, void *data, Screen *opens,
               int index) {
    auto tile = std::make_unique<Tile>();
    tile->key = Text(title);
    tile->icon = icon && *icon ? icon : "menu_mainsettings";
    tile->onPressed = onPressed;
    tile->data = data;
    tile->opens = opens;
    tile->index = index;
    g_TileByKey.emplace(tile->key, tile.get());
    tiles.push_back(std::move(tile));
}

void AddTile(Location where, const char *title, const char *icon, Callback onPressed, void *data, int index) {
    AddTileTo(g_Tiles[static_cast<int>(where)], title, icon, onPressed, data, nullptr, index);
}

void AddTile(Location where, const char *title, const char *icon, Screen *opens, int index) {
    AddTileTo(g_Tiles[static_cast<int>(where)], title, icon, nullptr, nullptr, opens, index);
}

} // namespace Menu
