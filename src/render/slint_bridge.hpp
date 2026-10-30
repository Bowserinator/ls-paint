#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <slint.h>

#include "render/preview_cache.hpp"

namespace lsp {

// A tile image and the document area it covers. `clip` is the part of the
// image that fills the area.
struct TilePlacement {
    slint::Image image;
    PreviewKey key;
    std::uint64_t revision = 0;
    RectI area;
    RectI clip;
    int bleed_x = 0;
    int bleed_y = 0;
    bool pixelated = false;
};

// Whether two placements show the same picture in the same place.
[[nodiscard]] inline bool same_picture(const TilePlacement& a, const TilePlacement& b) {
    return a.key == b.key && a.revision == b.revision && a.area == b.area && a.clip == b.clip &&
           a.bleed_x == b.bleed_x && a.bleed_y == b.bleed_y && a.pixelated == b.pixelated;
}

struct CanvasPresentation {
    std::vector<TilePlacement> tiles;
    bool ready = false;
    bool failed = false;
};

// Turns the document into the tile images the canvas shows, and keeps them
// current as the document is edited.
class SlintBridge {
public:
    SlintBridge();
    ~SlintBridge();
    void shutdown();
    // `wake` is called when more work is waiting, `present` when the tiles changed.
    void attach(std::function<void()> wake, std::function<void()> present);
    void upload(const PixelBuffer& source, RectI region, std::uint64_t generation);
    bool set_zoom(float zoom, float dpr);
    void set_view(const PixelBuffer& source, RectI view);
    void poll(const PixelBuffer& source);
    // Whether every tile the view needs has been generated.
    [[nodiscard]] bool idle() const;
    [[nodiscard]] float zoom() const;
    [[nodiscard]] float device_ratio() const;
    [[nodiscard]] const CanvasPresentation& presentation() const;
    [[nodiscard]] std::size_t cache_bytes() const;

private:
    struct State;
    std::shared_ptr<State> m_state;
};

[[nodiscard]] slint::Image image_from(const PixelBuffer& source);

}  // namespace lsp
