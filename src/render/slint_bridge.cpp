#include "render/slint_bridge.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>

#include "ui/debug_stats.hpp"
#include "utils/trace.hpp"

namespace lsp {
namespace {

slint::Image image_from_grid(const PixelGrid& grid) {
    const ScopedPhase timing(Phase::Images);
    trace::bump(trace::TileImage);
    slint::SharedPixelBuffer<slint::Rgba8Pixel> pixels(static_cast<std::uint32_t>(grid.width),
                                                     static_cast<std::uint32_t>(grid.height));
    static_assert(sizeof(Rgba8) == sizeof(slint::Rgba8Pixel));
    if (!grid.pixels.empty()) std::memcpy(pixels.begin(), grid.pixels.data(), grid.pixels.size() * sizeof(Rgba8));
    return slint::Image(std::move(pixels));
}

}  // namespace

struct SlintBridge::State {
    struct Picture {
        std::uint64_t revision = 0;
        slint::Image image;
    };

    PreviewCache cache;
    std::map<PreviewKey, Picture> pictures;
    std::function<void()> wake;
    std::function<void()> present;
    CanvasPresentation published;
    RectI area;
    float zoom = 1.0f, dpr = 1.0f;
    std::chrono::steady_clock::time_point moved;
    std::uint64_t shown_version = 0;
    double shown_scale = 0;
    bool busy = false, scheduled = false, alive = true, republish = true;

    [[nodiscard]] bool moving() const {
        return std::chrono::steady_clock::now() - moved < std::chrono::milliseconds(kRenderStillMilliseconds);
    }

    void publish() {
        const ScopedPhase timing(Phase::Publish);
        const double scale = double(zoom) * dpr;
        if (!republish && cache.version() == shown_version && scale == shown_scale) return;
        shown_version = cache.version();
        shown_scale = scale;
        std::map<PreviewKey, Picture> kept;
        std::vector<TilePlacement> tiles;
        for (const PreviewPatch& patch : cache.plan()) {
            const PreviewKey key = patch.tile->key;
            auto it = kept.find(key);
            if (it == kept.end()) {
                const auto old = pictures.find(key);
                Picture picture = old != pictures.end() && old->second.revision == patch.tile->revision
                                      ? std::move(old->second)
                                      : Picture{patch.tile->revision, image_from_grid(patch.tile->pixels)};
                it = kept.emplace(key, std::move(picture)).first;
            }
            tiles.push_back({it->second.image, key, patch.tile->revision, patch.area, patch.clip,
                             patch.bleed_x, patch.bleed_y, scale >= 1.0 && key.level == 0});
        }
        pictures = std::move(kept);

        bool changed = republish || tiles.size() != published.tiles.size();
        for (std::size_t i = 0; !changed && i < tiles.size(); ++i) changed = !same_picture(tiles[i], published.tiles[i]);
        const bool ready = published.ready || !tiles.empty();
        const bool failed = cache.failed();
        if (ready != published.ready || failed != published.failed) changed = true;
        if (!changed) return;
        republish = false;
        published.tiles = std::move(tiles);
        published.ready = ready;
        published.failed = failed;
        if (present) present();
    }
};

SlintBridge::SlintBridge() : m_state(std::make_shared<State>()) {}

SlintBridge::~SlintBridge() { shutdown(); }

void SlintBridge::shutdown() {
    if (!m_state || !m_state->alive) return;
    m_state->alive = false;
    m_state->wake = {};
    m_state->present = {};
}

void SlintBridge::attach(std::function<void()> wake, std::function<void()> present) {
    m_state->wake = std::move(wake);
    m_state->present = std::move(present);
}

void SlintBridge::upload(const PixelBuffer& source, RectI region, std::uint64_t generation) {
    upload(source, std::span<const RectI>(&region, 1), generation);
}

void SlintBridge::upload(const PixelBuffer& source, std::span<const RectI> regions, std::uint64_t generation) {
    const ScopedPhase timing(Phase::Upload);
    auto& state = *m_state;
    if (state.cache.generation() != generation || state.cache.bounds() != source.bounds()) {
        state.cache.reset(source.bounds(), generation);
        state.pictures.clear();
        state.published = {};
        state.republish = true;
    } else {
        const ScopedPhase invalidating(Phase::Invalidate);
        state.cache.invalidate(source, regions);
    }
    poll(source);
}

bool SlintBridge::set_zoom(float zoom, float dpr) {
    if (!std::isfinite(zoom) || zoom <= 0) zoom = 1.0f;
    if (!std::isfinite(dpr) || dpr <= 0) dpr = 1.0f;
    if (zoom == m_state->zoom && dpr == m_state->dpr) return false;
    m_state->zoom = zoom;
    m_state->dpr = dpr;
    m_state->moved = std::chrono::steady_clock::now();
    return true;
}

void SlintBridge::set_view(const PixelBuffer& source, RectI view) {
    if (view != m_state->area) m_state->moved = std::chrono::steady_clock::now();
    m_state->area = view;
    poll(source);
}

void SlintBridge::poll(const PixelBuffer& source) {
    const ScopedPhase timing(Phase::Poll);
    auto& state = *m_state;
    if (!state.alive || state.cache.bounds().empty() || state.cache.bounds() != source.bounds()) return;
    const RectI area = (state.area.empty() ? source.bounds() : state.area).intersected(source.bounds());
    state.cache.demand(area, double(state.zoom) * state.dpr);
    const bool moving = state.moving();
    {
        const ScopedPhase building(Phase::Build);
        state.busy = state.cache.build(
            source, std::chrono::milliseconds(moving ? kRenderBusyMilliseconds : kRenderIdleMilliseconds));
    }
    state.publish();
    if (!state.busy || state.scheduled || !state.wake) return;
    state.scheduled = true;
    const std::weak_ptr<State> weak = m_state;
    slint::Timer::single_shot(std::chrono::milliseconds(moving ? 8 : 0), [weak] {
        const auto owner = weak.lock();
        if (!owner || !owner->alive) return;
        owner->scheduled = false;
        if (owner->wake) owner->wake();
    });
}

bool SlintBridge::idle() const { return !m_state->busy; }
float SlintBridge::zoom() const { return m_state->zoom; }
float SlintBridge::device_ratio() const { return m_state->dpr; }
const CanvasPresentation& SlintBridge::presentation() const { return m_state->published; }
std::size_t SlintBridge::cache_bytes() const { return m_state->cache.bytes(); }

slint::Image image_from(const PixelBuffer& source, RectI region) {
    region = region.intersected(source.bounds());
    trace::bump(trace::ImageFrom);
    return image_from_grid({region.width, region.height, source.contiguous_copy(region)});
}

slint::Image image_from(const PixelBuffer& source) {
    trace::bump(trace::ImageFrom);
    return image_from_grid({source.width(), source.height(), source.contiguous_copy(source.bounds())});
}

}  // namespace lsp
