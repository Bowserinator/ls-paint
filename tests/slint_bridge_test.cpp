#include <cstdio>

#include "render/slint_bridge.hpp"

using namespace lsp;

int main() {
    PixelBuffer paper(128, 9000);
    for (int y = 0; y < paper.height(); ++y) {
        for (int x = 0; x < paper.width(); ++x) {
            paper.at(x, y) = {static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y),
                              static_cast<std::uint8_t>(x + y), static_cast<std::uint8_t>(x ^ y)};
        }
    }
    const PixelBuffer shared = paper;
    for (const RectI region : {paper.bounds(), RectI{7, 8190, 13, 7}, RectI{-5, 8190, 12, 7},
                               RectI{127, 8999, 10, 10}, RectI{500, 10000, 10, 10}, RectI{}}) {
        const RectI clipped = region.intersected(paper.bounds());
        const slint::Image image = image_from(paper, region);
        if (image.size().width != static_cast<unsigned>(clipped.width) ||
            image.size().height != static_cast<unsigned>(clipped.height)) return 1;
        if (clipped.empty()) continue;
        const auto pixels = image.to_rgba8();
        if (!pixels) return 1;
        for (int y = 0; y < clipped.height; ++y) {
            for (int x = 0; x < clipped.width; ++x) {
                const Rgba8 expected = shared.at(x + clipped.x, y + clipped.y);
                const auto actual = pixels->begin()[static_cast<std::size_t>(y) * clipped.width + x];
                if (actual.r != expected.r || actual.g != expected.g || actual.b != expected.b ||
                    actual.a != expected.a) return 1;
            }
            if (!paper.row_shared_with(shared, y + clipped.y)) return 1;
        }
    }
    const auto image = image_from(paper);
    paper.at(0, 0) = kWhite;
    const auto pixels = image.to_rgba8();
    if (!pixels || pixels->begin()->r != 0 || pixels->begin()->a != 0) return 1;
    std::puts("All Slint bridge image tests passed");
}
