#include <atomic>
#include <cstdio>
#include <random>

#include "core/canvas_document.hpp"
#include "core/pixel_delta.hpp"
#include "selection/selection_ops.hpp"
#include "utils/parallel.hpp"

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

Rgba8 random_color(std::mt19937& rng) {
    return {static_cast<std::uint8_t>(rng()), static_cast<std::uint8_t>(rng()), static_cast<std::uint8_t>(rng()), 255};
}

void scribble(PixelBuffer& pixels, std::mt19937& rng, int strokes) {
    for (int i = 0; i < strokes; ++i) {
        const int x = static_cast<int>(rng() % static_cast<unsigned>(pixels.width()));
        const int y = static_cast<int>(rng() % static_cast<unsigned>(pixels.height()));
        const int w = 1 + static_cast<int>(rng() % 40);
        const int h = 1 + static_cast<int>(rng() % 40);
        const Rgba8 color = random_color(rng);
        for (int yy = y; yy < std::min(pixels.height(), y + h); ++yy)
            for (int xx = x; xx < std::min(pixels.width(), x + w); ++xx) pixels.at(xx, yy) = color;
    }
}

// With nothing floating, the flattened image is the layer itself.
void flattened_matches_layer() {
    std::mt19937 rng(7);
    CanvasDocument doc(300, 200);
    scribble(doc.active_layer().pixels, rng, 60);
    doc.mark_dirty(doc.bounds());
    (void)doc.flush();

    CHECK(doc.flattened() == doc.active_layer().pixels);
    // The picture is the layer's own storage, not a second copy.
    CHECK(doc.flattened().storage_bytes() == doc.active_layer().pixels.storage_bytes());
    CHECK(doc.flattened().row_shared_with(doc.active_layer().pixels, 10));

    // A floating block is drawn over a copy, and moving it leaves no trace.
    PixelBuffer block(20, 20, Rgba8{9, 8, 7, 255});
    doc.floating() = float_block(block, std::vector<std::uint8_t>(400, 1), 50, 50);
    doc.mark_dirty(doc.bounds());
    (void)doc.flush();
    CHECK((doc.flattened().at(55, 55) == Rgba8{9, 8, 7, 255}));
    CHECK(!(doc.active_layer().pixels.at(55, 55) == Rgba8{9, 8, 7, 255}));

    doc.floating()->position.x = 120;
    doc.floating()->position.y = 90;
    doc.mark_dirty({40, 40, 120, 90});
    (void)doc.flush();
    CHECK(doc.flattened().at(55, 55) == doc.active_layer().pixels.at(55, 55));
    CHECK((doc.flattened().at(125, 95) == Rgba8{9, 8, 7, 255}));
    // Only the bands the block touched were copied.
    CHECK(doc.flattened().storage_bytes() < 2 * doc.active_layer().pixels.storage_bytes());

    doc.floating().reset();
    doc.mark_dirty(doc.bounds());
    (void)doc.flush();
    CHECK(doc.flattened() == doc.active_layer().pixels);
    CHECK(doc.flattened().row_shared_with(doc.active_layer().pixels, 10));
}

PixelBuffer naive_cw(const PixelBuffer& source) {
    PixelBuffer result(source.height(), source.width());
    for (int y = 0; y < source.height(); ++y)
        for (int x = 0; x < source.width(); ++x) result.at(source.height() - 1 - y, x) = source.at(x, y);
    return result;
}

PixelBuffer naive_ccw(const PixelBuffer& source) {
    PixelBuffer result(source.height(), source.width());
    for (int y = 0; y < source.height(); ++y)
        for (int x = 0; x < source.width(); ++x) result.at(y, source.width() - 1 - x) = source.at(x, y);
    return result;
}

void rotation_matches_reference() {
    std::mt19937 rng(11);
    for (const auto [w, h] : {std::pair{1, 1}, {7, 3}, {33, 65}, {200, 131}, {1500, 900}}) {
        PixelBuffer pixels(w, h);
        scribble(pixels, rng, 200);
        CHECK(pixels.rotated_90_cw() == naive_cw(pixels));
        CHECK(pixels.rotated_90_ccw() == naive_ccw(pixels));
        CHECK(pixels.rotated_90_cw().rotated_90_ccw() == pixels);
    }
}

// Undo and redo of a delta give back exactly the two versions it was made from,
// whether it kept squares or whole bands.
void delta_round_trips(RectI region, int strokes, bool expect_bands) {
    std::mt19937 rng(3);
    PixelBuffer before(1000, 700);
    scribble(before, rng, 80);
    PixelBuffer after = before;
    if (strokes < 0) {
        for (int y = region.y; y < region.bottom(); ++y)
            for (int x = region.x; x < region.right(); ++x) after.at(x, y) = {1, 2, 3, 255};
    } else {
        scribble(after, rng, strokes);
    }
    const PixelDelta delta = PixelDelta::between(before, after, region.width > 0 ? region : before.bounds());
    PixelBuffer work = after;
    delta.undo(work);
    CHECK(work == before);
    delta.redo(work);
    CHECK(work == after);
    // A change that rewrote bands costs their storage, not a copy of the pixels.
    if (expect_bands) CHECK(delta.memory_bytes() <= 2 * after.storage_bytes());
}

void whole_canvas_fill_is_cheap() {
    PixelBuffer before(2048, 2048, kWhite);
    PixelBuffer after = before;
    for (int y = 0; y < after.height(); ++y)
        for (auto& pixel : after.row(y)) pixel = {10, 20, 30, 255};
    const PixelDelta delta = PixelDelta::between(before, after, before.bounds());
    const std::size_t image = after.storage_bytes();
    // The old tile copies took two images' worth. Here the untouched canvas is
    // one band of storage shared by every band, so the entry holds the new
    // image plus that single band.
    CHECK(delta.memory_bytes() <= image + image / 4);
    PixelBuffer work = after;
    delta.undo(work);
    CHECK(work == before);
    delta.redo(work);
    CHECK(work == after);
}

void parallel_loop_runs_every_item_once() {
    std::vector<int> hits(10000, 0);
    parallel_for(hits.size(), [&](std::size_t i) { ++hits[i]; });
    bool all_once = true;
    for (const int hit : hits) all_once = all_once && hit == 1;
    CHECK(all_once);
    // A loop inside a loop finishes too.
    std::atomic<int> total{0};
    parallel_for(8, [&](std::size_t) { parallel_for(8, [&](std::size_t) { ++total; }); });
    CHECK(total == 64);
}

}  // namespace

int main() {
    flattened_matches_layer();
    rotation_matches_reference();
    delta_round_trips({0, 0, 0, 0}, 40, false);
    delta_round_trips({0, 0, 1000, 700}, -1, true);
    delta_round_trips({0, 100, 1000, 300}, -1, true);
    delta_round_trips({13, 30, 333, 400}, -1, false);
    whole_canvas_fill_is_cheap();
    parallel_loop_runs_every_item_once();
    if (failures == 0) std::puts("all large-file tests passed");
    return failures == 0 ? 0 : 1;
}
