#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <vector>

namespace ImGuiShared {
inline constexpr char interfaceName[] = "AML.RenderWare.ImGui.v1";
inline constexpr uint32_t version = 1;
using Handle = uint64_t;

struct Rect {
    float left, top, right, bottom;
};
struct Touch {
    int32_t type, finger;
    float x, y;
};
struct Client {
    void *user;
    void (*frame)(void *, const Touch *, uint32_t, int32_t, int32_t, float);
    void (*reset)(void *);
};

// Only POD values and function pointers cross plugin boundaries; never share an ImGuiContext or STL object.
struct API {
    uint32_t version, bytes;
    void *state;
    Handle (*add)(void *, const Client *);
    void (*remove)(void *, Handle);
    void (*setRects)(void *, Handle, const Rect *, uint32_t);
};

// Cross-plugin draw order and touch routing. Add/Remove/SetRects/Frame/Reset run on the game's main thread;
// ProcessTouch may run on the input thread. Callbacks are invoked without the lock held.
class Coordinator {
  public:
    Handle Add(const Client &client);
    void Remove(Handle id);
    void SetRects(Handle id, const Rect *rects, uint32_t count);
    bool ProcessTouch(int type, int finger, float x, float y, int windowWidth, int windowHeight);
    void Frame(int width, int height, float deltaTime);
    void Reset();

  private:
    struct Entry {
        Handle id;
        Client client;
        std::vector<Rect> rects;
        std::vector<Touch> events;
        int primary = -1;
    };
    std::mutex mutex;
    std::vector<Entry> entries; // Back-to-front draw order.
    std::array<Handle, 32> owners{};
    std::array<bool, 32> captured{};
    Handle nextId = 1;
    int width = 0, height = 0;
};
} // namespace ImGuiShared
