#pragma once

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace lsp::trace {

// Set LSP_TRACE=1 to print, once a second, how often the interface was
// refreshed and how many images were built for the renderer to upload.
enum Counter { Refresh, Hover, OverlayImage, TileImage, TileRow, ImageFrom, CounterCount };

inline unsigned counts[CounterCount] = {};
inline unsigned totals[CounterCount] = {};

[[nodiscard]] inline bool enabled() {
    static const bool on = std::getenv("LSP_TRACE") != nullptr;
    return on;
}

[[nodiscard]] inline bool debug_enabled() {
    static const bool on = [] {
        const char* value = std::getenv("LS_DEBUG");
        return value != nullptr && std::string_view(value) == "1";
    }();
    return on;
}

inline void bump(Counter counter) {
    if (enabled()) ++counts[counter];
    if (debug_enabled()) ++totals[counter];
}

inline void report() {
    if (!enabled()) return;
    std::fprintf(stderr, "refresh=%u hover=%u overlay_img=%u tile_img=%u tile_rows=%u image_from=%u\n",
                 counts[Refresh], counts[Hover], counts[OverlayImage], counts[TileImage], counts[TileRow],
                 counts[ImageFrom]);
    for (unsigned& count : counts) count = 0;
}

}  // namespace lsp::trace
