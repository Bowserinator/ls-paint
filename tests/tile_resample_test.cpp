#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "render/tile_resample.hpp"

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

PixelGrid whole(const PixelBuffer& source, double scale) {
    const DeviceBox box{0, 0, static_cast<int>(source.width() * scale), static_cast<int>(source.height() * scale)};
    return resampled(source, box, scale);
}

const Rgba8& at(const PixelGrid& grid, int x, int y) { return grid.pixels[grid_index(x, y, grid.width)]; }

void levels() {
    CHECK(quick_level(1.0) == 0);
    CHECK(quick_level(0.8) == 0);
    CHECK(quick_level(0.7) == 1);
    CHECK(quick_level(0.5) == 1);
    CHECK(quick_level(0.125) == 3);

    CHECK(resample_level(0.9) == 0);
    CHECK(resample_level(0.5) == 0);
    CHECK(resample_level(0.25) == 1);
    CHECK(resample_level(0.125) == 2);

    // Past one level of halving the filter always reads at least two samples
    // either side, so it never degenerates to picking the nearest one.
    for (double scale = 0.9; scale > 0.1; scale *= 0.97) {
        const double radius = 1.0 / (scale * (1 << resample_level(scale)));
        CHECK(radius > 1.0);
        if (scale <= 0.5) CHECK(radius >= 2.0 - 1e-9 && radius < 4.0 + 1e-9);
    }
}

// The pixel counts the window's own layout arithmetic gives for the same tiles.
void tile_boxes() {
    constexpr int kTile = 512;
    // zoom 0.4, 1x screen, 1000 pixel canvas: edges 0, 205, then the exact 400.
    const float overlap = resample_overlap(0.4);
    CHECK(tile_device_box(0, 0, kTile, overlap, 0.4f, 1.0f, 1000, 1000) == (DeviceBox{0, 0, 207, 207}));
    CHECK(tile_device_box(1, 1, kTile, overlap, 0.4f, 1.0f, 1000, 1000) == (DeviceBox{205, 205, 195, 195}));
    // The same zoom on a 2x screen: scale 0.8, a 2.5 pixel overlap.
    const float overlap_2x = resample_overlap(0.8);
    CHECK(tile_device_box(0, 0, kTile, overlap_2x, 0.4f, 2.0f, 1000, 1000) == (DeviceBox{0, 0, 412, 412}));
    CHECK(tile_device_box(1, 0, kTile, overlap_2x, 0.4f, 2.0f, 1000, 1000) == (DeviceBox{410, 0, 390, 412}));

    // Every tile starts inside the one before it, so no gap opens.
    for (const float zoom : {0.125f, 0.3f, 0.41f, 0.64f, 0.99f}) {
        for (const float dpr : {1.0f, 1.25f, 2.0f}) {
            const double scale = static_cast<double>(zoom) * dpr;
            if (scale >= 1.0) continue;
            const float lap = resample_overlap(scale);
            for (int column = 0; column + 1 < 20; ++column) {
                const DeviceBox a = tile_device_box(column, 0, kTile, lap, zoom, dpr, 20 * kTile, kTile);
                const DeviceBox b = tile_device_box(column + 1, 0, kTile, lap, zoom, dpr, 20 * kTile, kTile);
                CHECK(a.x + a.width > b.x);
            }
        }
    }
}

// A one pixel line comes out as grey that carries the same ink, not as a solid
// black block.
void thin_line() {
    PixelBuffer source(64, 64, kWhite);
    for (int y = 0; y < 64; ++y) source.at(32, y) = Rgba8{0, 0, 0, 255};
    for (const double scale : {0.9, 0.7071, 0.5, 0.3536, 0.25}) {
        const PixelGrid out = whole(source, scale);
        int darkest = 255;
        double ink = 0.0;
        const int y = out.height / 2;
        for (int x = 2; x < out.width - 2; ++x) {
            darkest = std::min<int>(darkest, at(out, x, y).r);
            ink += 255 - at(out, x, y).r;
        }
        CHECK(darkest > 60);
        CHECK(darkest < 245);
        CHECK(std::abs(ink - 255.0 * scale) < 255.0 * scale * 0.15);
    }
}

// A one pixel checker averages to mid grey.
void checker() {
    PixelBuffer source(128, 128, kWhite);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            if ((x + y) % 2 == 0) source.at(x, y) = Rgba8{0, 0, 0, 255};
    for (const double scale : {0.7071, 0.5, 0.3536, 0.25, 0.125}) {
        const PixelGrid out = whole(source, scale);
        for (int y = 3; y < out.height - 3; ++y)
            for (int x = 3; x < out.width - 3; ++x) CHECK(std::abs(at(out, x, y).r - 128) <= 24);
    }
}

// Transparent pixels neither darken nor tint what they border.
void transparency() {
    PixelBuffer source(64, 64, kTransparent);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 32; ++x) source.at(x, y) = Rgba8{255, 0, 0, 255};
    const PixelGrid out = whole(source, 0.25);
    CHECK(at(out, 2, 8) == (Rgba8{255, 0, 0, 255}));
    CHECK(at(out, out.width - 2, 8).a == 0);
    bool saw_edge = false;
    for (int x = 0; x < out.width; ++x) {
        const Rgba8 p = at(out, x, 8);
        if (p.a > 0 && p.a < 255) {
            saw_edge = true;
            CHECK(p.r == 255 && p.g == 0 && p.b == 0);
        }
    }
    CHECK(saw_edge);
}

// Two tiles agree on the pixels they both cover, so no seam shows.
void tiles_agree() {
    constexpr int kTile = 512;
    PixelBuffer source(1300, 700, kWhite);
    std::srand(7);
    for (int y = 0; y < 700; ++y)
        for (int x = 0; x < 1300; ++x)
            source.at(x, y) = Rgba8{static_cast<std::uint8_t>(std::rand()), static_cast<std::uint8_t>(std::rand()),
                                    static_cast<std::uint8_t>(std::rand()), 255};
    for (const auto& [zoom, dpr] : {std::pair{0.3f, 1.0f}, std::pair{0.2f, 2.0f}, std::pair{0.45f, 1.25f}}) {
        const double scale = static_cast<double>(zoom) * dpr;
        const float lap = resample_overlap(scale);
        const DeviceBox a = tile_device_box(0, 0, kTile, lap, zoom, dpr, 1300, 700);
        const DeviceBox b = tile_device_box(1, 0, kTile, lap, zoom, dpr, 1300, 700);
        const PixelGrid left = resampled(source, a, scale);
        const PixelGrid right = resampled(source, b, scale);
        CHECK(a.x + a.width > b.x);
        for (int device = b.x; device < a.x + a.width; ++device)
            for (int y = 0; y < std::min(a.height, b.height); ++y)
                CHECK(at(left, device - a.x, y) == at(right, device - b.x, y));
    }
}

// A screenful at the smallest zoom is quick enough to redo when the zoom settles.
void timing() {
    constexpr int kTile = 512;
    const int width = 15360;
    const int height = 8640;
    const PixelBuffer source(width, height, kWhite);
    const double scale = 0.125;
    const float lap = resample_overlap(scale);
    const auto start = std::chrono::steady_clock::now();
    long long touched = 0;
    for (int row = 0; row * kTile < height; ++row)
        for (int column = 0; column * kTile < width; ++column) {
            const DeviceBox box = tile_device_box(column, row, kTile, lap, 0.125f, 1.0f, width, height);
            touched += static_cast<long long>(resampled(source, box, scale).pixels.size());
        }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    std::printf("full screen resample at 0.125: %lld ms (%lld pixels)\n", static_cast<long long>(ms.count()), touched);
    CHECK(ms.count() < 10000);
}

}  // namespace

int main() {
    levels();
    tile_boxes();
    thin_line();
    checker();
    transparency();
    tiles_agree();
    timing();
    if (failures == 0) std::printf("all tile resample tests passed\n");
    return failures == 0 ? 0 : 1;
}
