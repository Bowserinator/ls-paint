#include "store/window_settings.hpp"

#include <algorithm>
#include <cstdint>

namespace lsp {

RectI restore_window_settings(Store& store, RectI defaults, std::span<const RectI> work_areas,
                              bool position_supported) {
    constexpr int kMinWidth = 620;
    constexpr int kMinHeight = 240;
    defaults.width = std::clamp(defaults.width, kMinWidth, kMaxImageDimension);
    defaults.height = std::clamp(defaults.height, kMinHeight, kMaxImageDimension);
    RectI geometry{stored_int(store, kStoreKeyWindowX).value_or(defaults.x),
                   stored_int(store, kStoreKeyWindowY).value_or(defaults.y),
                   std::clamp(stored_int(store, kStoreKeyWindowWidth).value_or(defaults.width),
                              kMinWidth, kMaxImageDimension),
                   std::clamp(stored_int(store, kStoreKeyWindowHeight).value_or(defaults.height),
                              kMinHeight, kMaxImageDimension)};

    const RectI* screen = nullptr;
    std::int64_t best_overlap = -1;
    for (const RectI& area : work_areas) {
        if (area.empty()) continue;
        const std::int64_t width = std::max<std::int64_t>(0,
            std::min(std::int64_t(geometry.x) + geometry.width, std::int64_t(area.x) + area.width) -
            std::max(geometry.x, area.x));
        const std::int64_t height = std::max<std::int64_t>(0,
            std::min(std::int64_t(geometry.y) + geometry.height, std::int64_t(area.y) + area.height) -
            std::max(geometry.y, area.y));
        const std::int64_t overlap = position_supported ? width * height : std::int64_t(area.width) * area.height;
        if (overlap > best_overlap) {
            screen = &area;
            best_overlap = overlap;
        }
    }
    if (screen) {
        geometry.width = std::min(geometry.width, screen->width);
        geometry.height = std::min(geometry.height, screen->height);
        geometry.x = static_cast<int>(std::clamp<std::int64_t>(geometry.x, screen->x,
            std::int64_t(screen->x) + screen->width - geometry.width));
        geometry.y = static_cast<int>(std::clamp<std::int64_t>(geometry.y, screen->y,
            std::int64_t(screen->y) + screen->height - geometry.height));
    } else {
        geometry = defaults;
    }

    bool repaired = false;
    const auto write = [&](std::string_view key, int value) {
        const std::string text = std::to_string(value);
        if (store.get(key) == text) return;
        store.set(key, text);
        repaired = true;
    };
    write(kStoreKeyWindowWidth, geometry.width);
    write(kStoreKeyWindowHeight, geometry.height);
    if (position_supported && screen) {
        write(kStoreKeyWindowX, geometry.x);
        write(kStoreKeyWindowY, geometry.y);
    } else {
        repaired = store.erase(kStoreKeyWindowX) || repaired;
        repaired = store.erase(kStoreKeyWindowY) || repaired;
    }
    if (repaired) store.save();
    return geometry;
}

}  // namespace lsp
