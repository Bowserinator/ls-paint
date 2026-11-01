#include "render/preview_cache.hpp"

#include <algorithm>
#include <cmath>
#include <new>

#include "utils/math.hpp"
#include "utils/tiles.hpp"

namespace lsp {
namespace {

RectI clip_of(const PreviewTile& tile, RectI area) {
    const int factor = 1 << tile.key.level;
    const int left = (area.x - tile.area.x) / factor;
    const int top = (area.y - tile.area.y) / factor;
    const int right = std::min(tile.pixels.width, ceil_div(area.right() - tile.area.x, factor));
    const int bottom = std::min(tile.pixels.height, ceil_div(area.bottom() - tile.area.y, factor));
    return {left, top, right - left, bottom - top};
}

}  // namespace

void PreviewCache::reset(RectI bounds, std::uint64_t generation) {
    m_entries.clear();
    m_wanted.clear();
    m_bounds = bounds;
    m_view = {};
    m_generation = generation;
    m_overview = {};
    while (ceil_div(std::max(bounds.width, bounds.height), 1 << m_overview.level) > kOverviewEdge)
        ++m_overview.level;
    m_level = m_overview.level;
    m_asked_level = -1;
    m_failed = false;
    ++m_changes;
}

RectI PreviewCache::core(PreviewKey key) const {
    const int side = kDisplayTileSize << key.level;
    return RectI{key.column * side, key.row * side, side, side}.intersected(m_bounds);
}

RectI PreviewCache::samples(PreviewKey key) const {
    const RectI area = core(key);
    const int gutter = kPreviewGutter << key.level;
    return RectI{area.x - gutter, area.y - gutter, area.width + 2 * gutter, area.height + 2 * gutter}
        .intersected(m_bounds);
}

RectI PreviewCache::extent(PreviewKey key) const {
    const RectI area = samples(key);
    const int factor = 1 << key.level;
    return {0, 0, ceil_div(area.width, factor), ceil_div(area.height, factor)};
}

std::size_t PreviewCache::bytes() const {
    std::size_t result = 0;
    for (const auto& [key, entry] : m_entries)
        if (entry.tile) result += entry.tile->pixels.pixels.size() * sizeof(Rgba8);
    return result;
}

bool PreviewCache::make_room(std::size_t extra, PreviewKey keep, bool new_entry) {
    while (bytes() + extra > m_budget || (new_entry && m_entries.size() >= kRenderMaxEntries)) {
        auto victim = m_entries.end();
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            if (it->first == m_overview || it->first == keep || it->second.used > m_pinned) continue;
            if (victim == m_entries.end() || it->second.used < victim->second.used) victim = it;
        }
        if (victim == m_entries.end()) return false;
        m_entries.erase(victim);
        ++m_changes;
    }
    return true;
}

PreviewCache::Entry* PreviewCache::ensure(PreviewKey key) {
    if (const auto it = m_entries.find(key); it != m_entries.end()) return &it->second;
    if (core(key).empty() || !make_room(0, key, true)) return nullptr;
    Entry& entry = m_entries[key];
    entry.stale = extent(key);
    return &entry;
}

void PreviewCache::refresh(PreviewKey key, Entry& entry, RectI region, const PixelBuffer& source) {
    const int factor = 1 << key.level;
    const RectI area = samples(key);
    const RectI input = RectI{area.x + region.x * factor, area.y + region.y * factor, region.width * factor,
                              region.height * factor}.intersected(area);
    if (input.empty()) return;
    const PixelGrid reduced = box_reduced(source, input, factor);
    PixelGrid& grid = entry.tile->pixels;
    for (int row = 0; row < reduced.height; ++row) {
        const auto from = reduced.pixels.begin() + static_cast<std::ptrdiff_t>(grid_index(0, row, reduced.width));
        std::copy_n(from, reduced.width,
                    grid.pixels.begin() + static_cast<std::ptrdiff_t>(grid_index(region.x, region.y + row, grid.width)));
    }
    entry.tile->revision = ++m_serial;
    ++m_changes;
}

bool PreviewCache::advance(PreviewKey key, Entry& entry, const PixelBuffer& source) {
    const RectI size = extent(key);
    try {
        if (!entry.tile) {
            const auto count = static_cast<std::size_t>(size.width) * static_cast<std::size_t>(size.height);
            if (!make_room(count * sizeof(Rgba8), key, false)) return false;
            auto tile = std::make_shared<PreviewTile>();
            tile->key = key;
            tile->area = samples(key);
            tile->pixels = {size.width, size.height, std::vector<Rgba8>(count)};
            entry.tile = std::move(tile);
        }
        const RectI stale = entry.stale.intersected(size);
        if (!stale.empty()) {
            const std::size_t factor = std::size_t{1} << key.level;
            const std::size_t row_pixels = static_cast<std::size_t>(stale.width) * factor * factor;
            const int rows = static_cast<int>(
                std::clamp<std::size_t>(kRenderChunkBytes / sizeof(Rgba8) / row_pixels, 1, stale.height));
            refresh(key, entry, {stale.x, stale.y, stale.width, rows}, source);
            entry.stale = {stale.x, stale.y + rows, stale.width, stale.height - rows};
        }
        if (entry.stale.intersected(size).empty()) {
            entry.stale = {};
            entry.complete = true;
            if (entry.tile->revision == 0) entry.tile->revision = ++m_serial;
            ++m_changes;
        }
        return true;
    } catch (const std::bad_alloc&) {
        m_failed = true;
        return false;
    }
}

void PreviewCache::invalidate(const PixelBuffer& source, RectI dirty) {
    invalidate(source, std::span<const RectI>(&dirty, 1));
}

void PreviewCache::invalidate(const PixelBuffer& source, std::span<const RectI> dirty) {
    if (dirty.empty() || source.bounds() != m_bounds) return;
    std::size_t budget = kPreviewPatchBytes / sizeof(Rgba8);
    const auto patch = [&](PreviewKey key, Entry& entry) {
        if (!entry.tile) return;
        const RectI area = samples(key);
        RectI hit;
        for (const RectI region : dirty) hit = hit.united(area.intersected(region));
        if (hit.empty()) return;
        const int factor = 1 << key.level;
        const int left = (hit.x - area.x) / factor;
        const int top = (hit.y - area.y) / factor;
        const RectI region = RectI{left, top, ceil_div(hit.right() - area.x, factor) - left,
                                   ceil_div(hit.bottom() - area.y, factor) - top}.intersected(extent(key));
        const std::size_t cost = static_cast<std::size_t>(region.width) * static_cast<std::size_t>(region.height) *
                                 static_cast<std::size_t>(factor) * static_cast<std::size_t>(factor);
        if (entry.complete && cost <= budget) {
            try {
                refresh(key, entry, region, source);
                budget -= cost;
                return;
            } catch (const std::bad_alloc&) {
                m_failed = true;
            }
        }
        entry.stale = entry.stale.united(region);
    };
    for (auto& [key, entry] : m_entries)
        if (entry.used > m_pinned) patch(key, entry);
    for (auto& [key, entry] : m_entries)
        if (entry.used <= m_pinned) patch(key, entry);
}

void PreviewCache::demand(RectI view, double scale) {
    m_view = view.intersected(m_bounds);
    if (!std::isfinite(scale) || scale <= 0.0) scale = 1.0;
    const double desired = std::max(0.0, std::log2(1.0 / scale));
    while (m_level < m_overview.level && desired > m_level + 1.15) ++m_level;
    while (m_level > 0 && desired < m_level - 0.15) --m_level;
    if (m_view == m_asked_view && m_level == m_asked_level) return;
    m_asked_view = m_view;
    m_asked_level = m_level;
    ++m_changes;

    m_wanted.clear();
    const int side = kDisplayTileSize << m_level;
    const auto add = [&](PreviewKey key) {
        if (m_wanted.size() < kRenderMaxEntries && std::ranges::find(m_wanted, key) == m_wanted.end())
            m_wanted.push_back(key);
    };
    const auto add_area = [&](RectI area) {
        for_each_tile(area.intersected(m_bounds), side, [&](int column, int row) { add({m_level, column, row}); });
    };
    add_area(m_view);
    add(m_overview);
    const int margin = kRenderPrefetchPixels << m_level;
    add_area({m_view.x - margin, m_view.y - margin, m_view.width + 2 * margin, m_view.height + 2 * margin});

    m_pinned = m_clock;
    for (const PreviewKey key : m_wanted)
        if (const auto it = m_entries.find(key); it != m_entries.end()) it->second.used = ++m_clock;
    for (const PreviewKey key : m_wanted)
        if (!m_entries.contains(key))
            if (Entry* entry = ensure(key)) entry->used = ++m_clock;
}

bool PreviewCache::build(const PixelBuffer& source, std::chrono::steady_clock::duration budget) {
    if (source.bounds() != m_bounds) return false;
    const auto started = std::chrono::steady_clock::now();
    for (const PreviewKey key : m_wanted) {
        const auto it = m_entries.find(key);
        if (it == m_entries.end()) continue;
        Entry& entry = it->second;
        while (!entry.complete || !entry.stale.empty()) {
            if (!advance(key, entry, source)) break;
            if (std::chrono::steady_clock::now() - started >= budget) return true;
        }
    }
    return false;
}

void PreviewCache::add_patch(std::vector<PreviewPatch>& patches, RectI area,
                             const std::shared_ptr<PreviewTile>& tile) const {
    const RectI clip = clip_of(*tile, area);
    patches.push_back({area, clip, area.right() < m_bounds.right() && clip.right() < tile->pixels.width ? 1 : 0,
                       area.bottom() < m_bounds.bottom() && clip.bottom() < tile->pixels.height ? 1 : 0, tile});
}

std::vector<PreviewPatch> PreviewCache::plan() const {
    std::vector<PreviewPatch> result;
    const int side = kDisplayTileSize << m_level;
    const auto done = [&](PreviewKey key) -> const Entry* {
        const auto it = m_entries.find(key);
        return it != m_entries.end() && it->second.complete ? &it->second : nullptr;
    };
    for_each_tile(m_view, side, [&](int column, int row) {
        const PreviewKey target{m_level, column, row};
        if (const Entry* entry = done(target)) {
            add_patch(result, core(target), entry->tile);
            return;
        }

        if (m_level > 0) {
            std::vector<PreviewPatch> finer;
            bool covered = true;
            for (int dy = 0; dy < 2 && covered; ++dy)
                for (int dx = 0; dx < 2 && covered; ++dx) {
                    const PreviewKey child{m_level - 1, column * 2 + dx, row * 2 + dy};
                    if (core(child).empty()) continue;
                    const Entry* entry = done(child);
                    if (entry) add_patch(finer, core(child), entry->tile);
                    covered = entry != nullptr;
                }
            if (covered && !finer.empty()) {
                result.insert(result.end(), finer.begin(), finer.end());
                return;
            }
        }

        PreviewKey key = target;
        for (++key.level, key.column /= 2, key.row /= 2; key.level <= m_overview.level;
             ++key.level, key.column /= 2, key.row /= 2)
            if (const Entry* entry = done(key)) {
                add_patch(result, core(target), entry->tile);
                return;
            }
    });
    return result;
}

}  // namespace lsp
