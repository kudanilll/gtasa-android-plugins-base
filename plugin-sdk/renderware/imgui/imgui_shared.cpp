#include "imgui_shared.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ImGuiShared {
Handle Coordinator::Add(const Client &client) {
    if (!client.frame || !client.reset)
        return 0;
    const std::lock_guard<std::mutex> lock(mutex);
    const Handle id = nextId++;
    entries.push_back({id, client, {}, {}, -1});
    return id;
}

void Coordinator::Remove(Handle id) {
    const std::lock_guard<std::mutex> lock(mutex);
    entries.erase(std::remove_if(entries.begin(), entries.end(), [id](const Entry &entry) { return entry.id == id; }),
                  entries.end());
    // Keep captured gestures consumed until release: the game never received their press.
}

void Coordinator::SetRects(Handle id, const Rect *rects, uint32_t count) {
    const std::lock_guard<std::mutex> lock(mutex);
    const auto entry = std::find_if(entries.begin(), entries.end(), [id](const Entry &e) { return e.id == id; });
    if (entry == entries.end())
        return;
    entry->rects.clear();
    if (!rects)
        return;
    for (uint32_t i = 0; i < count; ++i) {
        const auto &r = rects[i];
        if (std::isfinite(r.left) && std::isfinite(r.top) && std::isfinite(r.right) && std::isfinite(r.bottom) &&
            r.left < r.right && r.top < r.bottom)
            entry->rects.push_back(r);
    }
}

bool Coordinator::ProcessTouch(int type, int finger, float x, float y, int windowWidth, int windowHeight) {
    if (finger < 0 || finger >= static_cast<int>(owners.size()) || !std::isfinite(x) || !std::isfinite(y))
        return false;
    const std::lock_guard<std::mutex> lock(mutex);
    if (windowWidth > 0 && windowHeight > 0 && width > 0 && height > 0) {
        x *= static_cast<float>(width) / windowWidth;
        y *= static_cast<float>(height) / windowHeight;
    }
    if (type == 2) {
        captured[finger] = false;
        owners[finger] = 0;
        for (size_t i = entries.size(); i > 0; --i) {
            auto &entry = entries[i - 1];
            const bool hit = std::any_of(entry.rects.begin(), entry.rects.end(), [x, y](const Rect &r) {
                return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
            });
            if (!hit)
                continue;
            captured[finger] = true;
            owners[finger] = entry.id;
            if (entry.primary < 0)
                entry.primary = finger;
            // Focus the clicked plugin by drawing it last. This runs once per press, never per move/release.
            std::rotate(entries.begin() + static_cast<ptrdiff_t>(i - 1), entries.begin() + static_cast<ptrdiff_t>(i),
                        entries.end());
            break;
        }
    }
    const bool consumed = captured[finger];
    const auto entry =
        std::find_if(entries.begin(), entries.end(), [&](const Entry &e) { return e.id == owners[finger]; });
    if (consumed && entry != entries.end() && entry->primary == finger && type >= 1 && type <= 4) {
        if (type == 3 && !entry->events.empty() && entry->events.back().type == 3)
            entry->events.back() = {type, finger, x, y};
        else
            entry->events.push_back({type, finger, x, y});
    }
    if (type == 1 || type == 4) {
        if (entry != entries.end() && entry->primary == finger)
            entry->primary = -1;
        captured[finger] = false;
        owners[finger] = 0;
    }
    return consumed;
}

void Coordinator::Frame(int newWidth, int newHeight, float deltaTime) {
    if (newWidth <= 0 || newHeight <= 0)
        return;
    std::vector<Entry> work;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        width = newWidth;
        height = newHeight;
        for (auto &entry : entries) {
            work.push_back({entry.id, entry.client, {}, {}, -1});
            work.back().events.swap(entry.events);
        }
    }
    for (const auto &entry : work) {
        bool alive;
        {
            const std::lock_guard<std::mutex> lock(mutex);
            alive = std::any_of(entries.begin(), entries.end(), [&](const Entry &e) { return e.id == entry.id; });
        }
        if (alive)
            entry.client.frame(entry.client.user, entry.events.data(), static_cast<uint32_t>(entry.events.size()),
                               newWidth, newHeight, deltaTime);
    }
}

void Coordinator::Reset() {
    std::vector<Client> clients;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        for (auto &entry : entries) {
            entry.rects.clear();
            entry.events.clear();
            entry.primary = -1;
            clients.push_back(entry.client);
        }
    }
    for (const auto &client : clients)
        client.reset(client.user);
}
} // namespace ImGuiShared
