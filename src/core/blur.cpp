#include "core/blur.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace lsp {

namespace {

Rgba8 premultiply(Rgba8 pixel) {
    const std::uint32_t alpha = pixel.a;
    return Rgba8{static_cast<std::uint8_t>(pixel.r * alpha / 255),
                 static_cast<std::uint8_t>(pixel.g * alpha / 255),
                 static_cast<std::uint8_t>(pixel.b * alpha / 255), pixel.a};
}

Rgba8 unpremultiply(Rgba8 pixel) {
    // An empty pixel stays empty: the default Rgba8 is opaque black.
    if (pixel.a == 0) return kTransparent;
    const std::uint32_t alpha = pixel.a;
    const auto restore = [alpha](std::uint8_t value) {
        return static_cast<std::uint8_t>(std::min<std::uint32_t>(255, value * 255 / alpha));
    };
    return Rgba8{restore(pixel.r), restore(pixel.g), restore(pixel.b), pixel.a};
}

// One box pass along a line of pixels. The window slides, so the cost is the
// length of the line, and both ends keep the colour they already have.
void box_line(std::span<Rgba8> line, int radius) {
    if (radius < 1 || line.size() < 2) return;

    const std::vector<Rgba8> source(line.begin(), line.end());
    const auto last = static_cast<std::ptrdiff_t>(source.size()) - 1;
    const auto sample = [&source, last](std::ptrdiff_t index) -> const Rgba8& {
        return source[static_cast<std::size_t>(std::clamp(index, std::ptrdiff_t{0}, last))];
    };

    std::int32_t r = 0;
    std::int32_t g = 0;
    std::int32_t b = 0;
    std::int32_t a = 0;
    for (int i = -radius; i <= radius; ++i) {
        const Rgba8& pixel = sample(i);
        r += pixel.r;
        g += pixel.g;
        b += pixel.b;
        a += pixel.a;
    }

    const auto span = static_cast<std::int32_t>(2 * radius + 1);
    for (std::ptrdiff_t i = 0; i <= last; ++i) {
        line[static_cast<std::size_t>(i)] =
            Rgba8{static_cast<std::uint8_t>(r / span), static_cast<std::uint8_t>(g / span),
                  static_cast<std::uint8_t>(b / span), static_cast<std::uint8_t>(a / span)};
        const Rgba8& leaving = sample(i - radius);
        const Rgba8& arriving = sample(i + radius + 1);
        r += static_cast<std::int32_t>(arriving.r) - leaving.r;
        g += static_cast<std::int32_t>(arriving.g) - leaving.g;
        b += static_cast<std::int32_t>(arriving.b) - leaving.b;
        a += static_cast<std::int32_t>(arriving.a) - leaving.a;
    }
}

void box_rows(PixelBuffer& image, int radius) {
    for (int y = 0; y < image.height(); ++y) box_line(image.row(y), radius);
}

template <typename PixelFn>
void for_each_pixel(PixelBuffer& image, PixelFn fn) {
    for (int y = 0; y < image.height(); ++y) {
        for (Rgba8& pixel : image.row(y)) pixel = fn(pixel);
    }
}

void box_columns(PixelBuffer& image, int radius) {
    std::vector<Rgba8> column(static_cast<std::size_t>(image.height()));
    for (int x = 0; x < image.width(); ++x) {
        for (int y = 0; y < image.height(); ++y) {
            column[static_cast<std::size_t>(y)] = image.at(x, y);
        }
        box_line(column, radius);
        for (int y = 0; y < image.height(); ++y) {
            image.at(x, y) = column[static_cast<std::size_t>(y)];
        }
    }
}

}  // namespace

PixelBuffer blur::box_twice(const PixelBuffer& source, int radius) {
    PixelBuffer image = source;
    if (radius < 1) return image;

    for_each_pixel(image, premultiply);

    for (int pass = 0; pass < 2; ++pass) {
        box_rows(image, radius);
        box_columns(image, radius);
    }

    for_each_pixel(image, unpremultiply);
    return image;
}

}  // namespace lsp
