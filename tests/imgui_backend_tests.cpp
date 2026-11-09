#include <cassert>
#include <climits>
#include <cstdio>
#include <plugin-sdk/renderware/imgui/imgui_impl_renderware_internal.h>

int main() {
    using namespace ImGuiRenderWareDetail;
    // AND_TouchEvent uses window pixels; the UI can render at a different resolution.
    const auto mapped = MapTouchToRender(40, 100, 1600, 720, 2400, 1080);
    assert(mapped.x == 60 && mapped.y == 150);
    const auto unchanged = MapTouchToRender(40, 100, 1600, 720, 1600, 720);
    assert(unchanged.x == 40 && unchanged.y == 100);
    const auto invalid = MapTouchToRender(-1, -1, 0, 0, 2400, 1080);
    assert(invalid.x == -1 && invalid.y == -1);
    const auto extreme = MapTouchToRender(INT_MAX, INT_MAX, 1, 1, INT_MAX, INT_MAX);
    assert(extreme.x == INT_MAX && extreme.y == INT_MAX);
    // Different source/destination pitches must preserve rows and destination padding.
    const unsigned char source[] = {1, 2, 3, 4, 99, 99, 99, 99, 5, 6, 7, 8};
    unsigned char destination[12]{};
    assert(CopyPixels(destination, 8, source, 8, 1, 2));
    assert(destination[0] == 1 && destination[8] == 5 && destination[4] == 0);
    assert(!CopyPixels(destination, 3, source, 8, 1, 2));
    assert(!CopyPixels(destination, 8, nullptr, 8, 1, 2));

    TouchInput input;
    assert(!input.Process(2, 0, 30, 40, true)); // UI not initialized.
    input.SetEnabled(true);
    assert(!input.Process(2, 0, 30, 40, false));
    assert(!input.Process(1, 0, 30, 40, true)); // Never steal the game's release.
    assert(input.Process(2, 0, 30, 40, true));
    assert(input.Process(2, 1, 50, 60, true)); // A second finger cannot release the primary.
    assert(input.Process(1, 1, 50, 60, true));
    assert(input.Process(1, 0, 30, 40, false)); // Capture persists outside the button/menu.
    auto events = input.Drain();
    assert(events.size() == 2 && events[0].type == 2 && events[1].type == 1);
    assert(input.Process(2, 2, 30, 40, true));
    assert(input.Process(4, 2, 30, 40, false)); // Cancel releases the primary.
    events = input.Drain();
    assert(events.size() == 2 && events.back().type == 4);
    assert(!input.Process(2, -1, 0, 0, true));
    assert(input.Process(2, 0, 30, 40, true));
    assert(input.Process(1, 0, 30, 40, true));
    input.SetEnabled(false);
    events = input.Drain();
    assert(events.size() == 1 && events[0].type == 1); // Pausing must discard pending clicks.
    std::puts("PASS: touch scaling, RGBA row strides, short taps, touch ownership, multitouch and pause reset");
}
