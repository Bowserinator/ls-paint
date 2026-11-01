#include <chrono>
#include <cstdio>
#include <map>
#include <utility>

#include "render/preview_cache.hpp"

using namespace lsp;

namespace {

int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

constexpr auto kForever = std::chrono::seconds(10);
constexpr int kTile = kDisplayTileSize;

constexpr int tiles_across(int length) { return (length + kTile - 1) / kTile; }

PixelBuffer pattern(int width, int height) {
    PixelBuffer source(width, height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            source.at(x, y) = {static_cast<std::uint8_t>(x * 7), static_cast<std::uint8_t>(y * 5),
                               static_cast<std::uint8_t>((x ^ y) * 3), 255};
    return source;
}

void settle(PreviewCache& cache, const PixelBuffer& source) {
    while (cache.build(source, kForever)) {}
}

// The pixel a patch shows for a document pixel.
Rgba8 shown(const PreviewPatch& patch, int x, int y) {
    const PreviewTile& tile = *patch.tile;
    const int factor = 1 << tile.key.level;
    return tile.pixels.pixels[grid_index((x - tile.area.x) / factor, (y - tile.area.y) / factor, tile.pixels.width)];
}

const PreviewPatch* patch_at(const std::vector<PreviewPatch>& plan, int x, int y) {
    for (const PreviewPatch& patch : plan)
        if (patch.area.contains(x, y)) return &patch;
    return nullptr;
}

PreviewCache make(const PixelBuffer& source, RectI view, double scale, std::size_t budget = kPreviewCacheBytes) {
    PreviewCache cache(budget);
    cache.reset(source.bounds(), 1);
    cache.demand(view, scale);
    settle(cache, source);
    return cache;
}

void full_size_matches_document() {
    const PixelBuffer source = pattern(1300, 700);
    const PreviewCache cache = make(source, source.bounds(), 1.0);
    CHECK(cache.level() == 0);
    const auto plan = cache.plan();
    CHECK(plan.size() == static_cast<std::size_t>(tiles_across(1300) * tiles_across(700)));
    for (const int x : {0, 1, kTile - 1, kTile, 2 * kTile - 1, 1299})
        for (const int y : {0, 3, kTile - 1, kTile, 699}) {
            const PreviewPatch* patch = patch_at(plan, x, y);
            CHECK(patch);
            if (patch) CHECK(shown(*patch, x, y) == source.at(x, y));
        }
    for (const PreviewPatch& patch : plan) {
        CHECK(patch.clip.width == patch.area.width);
        CHECK(patch.clip.height == patch.area.height);
    }
}

void reduced_levels_match_box_average() {
    for (const double scale : {0.5, 0.25, 0.1}) {
        const PixelBuffer source = pattern(1000, 333);
        const PreviewCache cache = make(source, source.bounds(), scale);
        const int factor = 1 << cache.level();
        CHECK(factor > 1);
        const PixelGrid expected = box_reduced(source, source.bounds(), factor);
        for (const PreviewPatch& patch : cache.plan()) {
            CHECK(patch.tile->key.level == cache.level());
            for (int y = patch.area.y; y < patch.area.bottom(); y += factor)
                for (int x = patch.area.x; x < patch.area.right(); x += factor)
                    CHECK(shown(patch, x, y) == expected.pixels[grid_index(x / factor, y / factor, expected.width)]);
        }
    }
}

void edits_show_without_building() {
    PixelBuffer source = pattern(1300, 700);
    for (const double scale : {1.0, 0.25}) {
        PreviewCache cache = make(source, source.bounds(), scale);
        const auto before = cache.plan();
        const PreviewPatch* old = patch_at(before, 610, 310);
        CHECK(old);
        const std::uint64_t old_revision = old ? old->tile->revision : 0;
        const int factor = 1 << cache.level();
        const RectI dirty{600, 300, 40, 40};
        for (int y = dirty.y; y < dirty.bottom(); ++y)
            for (int x = dirty.x; x < dirty.right(); ++x) source.at(x, y) = {255, 0, 0, 255};
        cache.invalidate(source, dirty);

        const auto after = cache.plan();
        const PreviewPatch* patch = patch_at(after, 610, 310);
        CHECK(patch);
        if (patch) {
            const Rgba8 pixel = shown(*patch, 610, 310);
            CHECK(pixel == (Rgba8{255, 0, 0, 255}));
            CHECK(patch->tile->revision != old_revision);
        }
        const PixelGrid expected = box_reduced(source, source.bounds(), factor);
        for (const PreviewPatch& each : after)
            for (int y = each.area.y; y < each.area.bottom(); y += factor)
                for (int x = each.area.x; x < each.area.right(); x += factor)
                    CHECK(shown(each, x, y) == expected.pixels[grid_index(x / factor, y / factor, expected.width)]);
        source = pattern(1300, 700);
    }
}

void edits_beside_a_tile_leave_it_alone() {
    PixelBuffer source = pattern(2048, 2048);
    PreviewCache cache = make(source, source.bounds(), 1.0);
    std::map<std::pair<int, int>, std::uint64_t> before;
    for (const PreviewPatch& patch : cache.plan()) before[{patch.area.x, patch.area.y}] = patch.tile->revision;
    source.at(10, 10) = {1, 2, 3, 255};
    cache.invalidate(source, {10, 10, 4, 4});
    for (const PreviewPatch& patch : cache.plan()) {
        const bool touched = patch.area.x == 0 && patch.area.y == 0;
        CHECK((patch.tile->revision != before[{patch.area.x, patch.area.y}]) == touched);
    }
}

void large_edits_catch_up() {
    PixelBuffer source = pattern(2048, 2048);
    PreviewCache cache = make(source, source.bounds(), 1.0);
    constexpr Rgba8 kGrey{9, 9, 9, 255};
    for (int y = 0; y < 2048; ++y)
        for (int x = 0; x < 2048; ++x) source.at(x, y) = kGrey;
    cache.invalidate(source, source.bounds());

    const auto count_current = [&] {
        int current = 0;
        for (const PreviewPatch& patch : cache.plan())
            if (shown(patch, patch.area.x + 1, patch.area.y + 1) == kGrey) ++current;
        return current;
    };
    const int total = tiles_across(2048) * tiles_across(2048);
    CHECK(count_current() > 0);
    CHECK(count_current() < total);

    settle(cache, source);
    CHECK(count_current() == total);
}

void batched_edits_refresh_each_tile_once() {
    for (const double scale : {1.0, 0.25}) {
        PixelBuffer source = pattern(1300, 700);
        PreviewCache cache = make(source, source.bounds(), scale);
        PreviewCache single = make(source, source.bounds(), scale);
        const RectI first{250, 250, 20, 20};
        const RectI second{760, 300, 20, 20};
        const std::vector<RectI> edits{first, second, first, {}, {-20, -20, 4, 4}};
        for (const RectI region : {first, second})
            for (int y = region.y; y < region.bottom(); ++y)
                for (int x = region.x; x < region.right(); ++x) source.at(x, y) = {255, 0, 0, 255};
        const auto version = cache.version();
        cache.invalidate(source, edits);
        single.invalidate(source, std::span<const RectI>(edits.data(), 2));
        CHECK(cache.version() - version == single.version() - version);
        CHECK(!cache.build(source, kForever));
        const int factor = 1 << cache.level();
        const PixelGrid expected = box_reduced(source, source.bounds(), factor);
        for (const PreviewPatch& patch : cache.plan())
            for (int y = patch.area.y; y < patch.area.bottom(); y += factor)
                for (int x = patch.area.x; x < patch.area.right(); x += factor)
                    CHECK(shown(patch, x, y) == expected.pixels[grid_index(x / factor, y / factor, expected.width)]);
    }
}

void batched_edits_share_the_patch_budget() {
    PixelBuffer source(2048, 2048, kWhite);
    PreviewCache cache = make(source, source.bounds(), 1.0);
    constexpr Rgba8 kRed{255, 0, 0, 255};
    source = PixelBuffer(2048, 2048, kRed);
    std::vector<RectI> edits;
    for (int y = 0; y < 2048; y += kTile)
        for (int x = 0; x < 2048; x += kTile) edits.push_back({x, y, kTile, kTile});
    cache.invalidate(source, edits);
    int current = 0;
    for (const PreviewPatch& patch : cache.plan())
        if (shown(patch, patch.area.x, patch.area.y) == kRed) ++current;
    CHECK(current > 0);
    CHECK(current < static_cast<int>(cache.plan().size()));
    settle(cache, source);
    for (const PreviewPatch& patch : cache.plan())
        CHECK(shown(patch, patch.area.x, patch.area.y) == kRed);
}

void coarser_tile_stands_in() {
    const PixelBuffer source = pattern(2048, 2048);
    const int far = 2048 - kTile;
    PreviewCache cache = make(source, {0, 0, kTile, kTile}, 1.0);
    cache.demand({far, 0, kTile, kTile}, 1.0);
    const auto plan = cache.plan();
    CHECK(plan.size() == 1);
    if (plan.empty()) return;
    const PreviewPatch& patch = plan.front();
    CHECK(patch.area == (RectI{far, 0, kTile, kTile}));
    int overview = 0;
    while ((2048 + (1 << overview) - 1) / (1 << overview) > kOverviewEdge) ++overview;
    CHECK(patch.tile->key.level == overview);
    const int factor = 1 << patch.tile->key.level;
    CHECK(patch.clip == (RectI{far / factor, 0, kTile / factor, kTile / factor}));

    settle(cache, source);
    CHECK(cache.plan().front().tile->key.level == 0);
}

void finer_tiles_stand_in_when_zooming_out() {
    const PixelBuffer source = pattern(2048, 2048);
    PreviewCache cache = make(source, {0, 0, 1024, 1024}, 1.0);
    cache.demand({0, 0, 1024, 1024}, 0.4);
    CHECK(cache.level() == 1);
    const auto plan = cache.plan();
    CHECK(plan.size() == static_cast<std::size_t>((1024 / kTile) * (1024 / kTile)));
    for (const PreviewPatch& patch : plan) {
        CHECK(patch.tile->key.level == 0);
        CHECK(patch.area.width == kTile);
        CHECK(shown(patch, patch.area.x + 3, patch.area.y + 5) == source.at(patch.area.x + 3, patch.area.y + 5));
    }
    settle(cache, source);
    const auto settled = cache.plan();
    CHECK(settled.size() == static_cast<std::size_t>((1024 / (2 * kTile)) * (1024 / (2 * kTile))));
    for (const PreviewPatch& patch : settled) CHECK(patch.tile->key.level == 1);
}

void bleed_stays_inside_the_document() {
    const PixelBuffer source = pattern(1300, 700);
    const PreviewCache cache = make(source, source.bounds(), 1.0);
    for (const PreviewPatch& patch : cache.plan()) {
        CHECK(patch.bleed_x == (patch.area.right() < 1300 ? 1 : 0));
        CHECK(patch.bleed_y == (patch.area.bottom() < 700 ? 1 : 0));
        CHECK(patch.clip.right() + patch.bleed_x <= patch.tile->pixels.width);
        CHECK(patch.clip.bottom() + patch.bleed_y <= patch.tile->pixels.height);
    }
}

void unchanged_demand_changes_nothing() {
    const PixelBuffer source = pattern(1300, 700);
    PreviewCache cache = make(source, source.bounds(), 1.0);
    const std::uint64_t version = cache.version();
    cache.demand(source.bounds(), 1.0);
    cache.demand(source.bounds(), 1.02);
    CHECK(!cache.build(source, kForever));
    CHECK(cache.version() == version);
    cache.invalidate(source, {1, 1, 2, 2});
    CHECK(cache.version() != version);
}

void memory_stays_bounded() {
    const PixelBuffer source(4096, 4096, kWhite);
    constexpr std::size_t budget = 12u * 1024u * 1024u;
    PreviewCache cache(budget);
    cache.reset(source.bounds(), 1);
    for (int step = 0; step < 8; ++step) {
        cache.demand({step * 512, step * 400, 1024, 768}, 1.0);
        settle(cache, source);
        CHECK(cache.bytes() <= budget);
        CHECK(!cache.plan().empty());
    }
}

void a_new_document_starts_over() {
    const PixelBuffer first(300, 300, kWhite);
    PreviewCache cache = make(first, first.bounds(), 1.0);
    CHECK(!cache.plan().empty());
    const PixelBuffer second(100, 80, Rgba8{1, 2, 3, 255});
    cache.reset(second.bounds(), 2);
    CHECK(cache.generation() == 2);
    CHECK(cache.entry_count() == 0);
    cache.demand(second.bounds(), 1.0);
    settle(cache, second);
    const auto plan = cache.plan();
    CHECK(plan.size() == 1);
    if (!plan.empty()) CHECK(shown(plan.front(), 99, 79) == (Rgba8{1, 2, 3, 255}));
}

}  // namespace

int main() {
    full_size_matches_document();
    reduced_levels_match_box_average();
    edits_show_without_building();
    edits_beside_a_tile_leave_it_alone();
    large_edits_catch_up();
    batched_edits_refresh_each_tile_once();
    batched_edits_share_the_patch_budget();
    coarser_tile_stands_in();
    finer_tiles_stand_in_when_zooming_out();
    bleed_stays_inside_the_document();
    unchanged_demand_changes_nothing();
    memory_stays_bounded();
    a_new_document_starts_over();
    if (failures == 0) std::printf("all preview cache tests passed\n");
    return failures == 0 ? 0 : 1;
}
