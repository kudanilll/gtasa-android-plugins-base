#pragma once

// Drag scrolling for SelectScreens, modelled on the game's KeyboardControlsScreen. Scroll values are negative:
// the game draws row 0 at GetStartY() + GetScrollY().

namespace Menu {

inline constexpr int kVisibleRows = 9; // Rows keep the height the game gives a 9-row list

constexpr float RowHeight(float areaHeight) { return areaHeight / kVisibleRows; }

// Furthest the list can scroll for `count` rows; 0 while they all fit.
constexpr float MinScroll(float areaHeight, int count) {
    const float overflow = areaHeight - RowHeight(areaHeight) * static_cast<float>(count);
    return overflow < 0.0f ? overflow : 0.0f;
}

constexpr float ClampScroll(float scrollY, float areaHeight, int count) {
    const float lowest = MinScroll(areaHeight, count);
    return scrollY > 0.0f ? 0.0f : (scrollY < lowest ? lowest : scrollY);
}

// True once a finger that went down at `startY` (0 = not on the list) has moved half a row.
constexpr bool IsDrag(float startY, float y, float rowHeight) {
    const float moved = y > startY ? y - startY : startY - y;
    return startY != 0.0f && moved > rowHeight * 0.5f;
}

// Latches a gesture as a drag, so coming back near the start does not turn it into a tap.
constexpr bool Dragged(bool dragged, float startY, float y, float rowHeight) {
    return dragged || IsDrag(startY, y, rowHeight);
}

// Turns on drag scrolling for a SelectScreen, ours or one of the game's tabs.
void EnableScrolling(void *selectScreen);

} // namespace Menu
