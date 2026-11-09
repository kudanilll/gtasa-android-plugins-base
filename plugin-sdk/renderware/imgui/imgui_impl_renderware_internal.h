#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <vector>

namespace ImGuiRenderWareDetail {

inline bool CopyPixels(unsigned char *dst, int dstPitch, const unsigned char *src, int srcPitch, int width,
                       int height) {
    if (!dst || !src || width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() / 4)
        return false;
    const int rowBytes = width * 4;
    if (dstPitch < rowBytes || srcPitch < rowBytes)
        return false;
    for (int y = 0; y < height; ++y)
        std::memcpy(dst + static_cast<size_t>(y) * dstPitch, src + static_cast<size_t>(y) * srcPitch, rowBytes);
    return true;
}

struct TouchEvent {
    int type;
    int x;
    int y;
};

struct TouchPoint {
    int x;
    int y;
};

inline TouchPoint MapTouchToRender(int x, int y, int screenWidth, int screenHeight, int renderWidth, int renderHeight) {
    if (x < 0 || y < 0 || screenWidth <= 0 || screenHeight <= 0 || renderWidth <= 0 || renderHeight <= 0)
        return {x, y};
    const auto scale = [](int value, int source, int target) {
        const auto result = static_cast<int64_t>(value) * target / source;
        return static_cast<int>(std::min(result, static_cast<int64_t>(std::numeric_limits<int>::max())));
    };
    return {scale(x, screenWidth, renderWidth), scale(y, screenHeight, renderHeight)};
}

class TouchInput {
  public:
    void SetEnabled(bool enabled) {
        const std::lock_guard<std::mutex> lock(mutex);
        if (this->enabled && !enabled) {
            events.clear();
            events.push_back({1, -1, -1});
            primary = -1;
        }
        this->enabled = enabled;
    }

    bool Process(int type, int finger, int x, int y, bool capture) {
        const std::lock_guard<std::mutex> lock(mutex);
        if (finger < 0 || finger >= static_cast<int>(owned.size()))
            return false;
        if (type == 2) {
            owned[finger] = enabled && capture;
            if (owned[finger] && primary < 0)
                primary = finger;
        }
        const bool consumed = owned[finger];
        if (consumed && finger == primary) {
            if (type == 1 || type == 2 || type == 3 || type == 4) {
                // Coalesce moves, but retain press/release pairs shorter than a frame.
                if (type == 3 && !events.empty() && events.back().type == 3)
                    events.back() = {type, x, y};
                else
                    events.push_back({type, x, y});
            }
        }
        if (type == 1 || type == 4) {
            owned[finger] = false;
            if (finger == primary)
                primary = -1;
        }
        return consumed;
    }

    std::vector<TouchEvent> Drain() {
        const std::lock_guard<std::mutex> lock(mutex);
        std::vector<TouchEvent> result;
        result.swap(events);
        return result;
    }

  private:
    std::mutex mutex;
    std::array<bool, 10> owned{};
    std::vector<TouchEvent> events;
    int primary = -1;
    bool enabled = false;
};

} // namespace ImGuiRenderWareDetail
