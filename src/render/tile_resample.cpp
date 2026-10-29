#include "render/tile_resample.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "utils/math.hpp"
#include "utils/parallel.hpp"

namespace lsp {

namespace {

constexpr int kMaxQuickLevel = 4;

// The source samples one row or column of output pixels reads, and how much
// each counts.
struct AxisTaps {
    std::vector<int> first;  // first sample read, before clamping to the grid
    std::vector<int> count;
    std::vector<std::size_t> offset;
    std::vector<float> weight;
    int lowest = 0;   // the samples all lie within [lowest, highest]
    int highest = 0;
};

// Tent weights of radius `radius` samples around each output pixel's centre.
AxisTaps axis_taps(int start, int length, double scale, int factor, double radius, int samples) {
    AxisTaps taps;
    taps.first.resize(static_cast<std::size_t>(length));
    taps.count.resize(static_cast<std::size_t>(length));
    taps.offset.resize(static_cast<std::size_t>(length));
    int lowest = samples - 1;
    int highest = 0;
    for (int i = 0; i < length; ++i) {
        const double centre = (start + i + 0.5) / scale / factor - 0.5;
        const int first = static_cast<int>(std::ceil(centre - radius));
        const int last = static_cast<int>(std::floor(centre + radius));
        const std::size_t at = static_cast<std::size_t>(i);
        taps.first[at] = first;
        taps.count[at] = last - first + 1;
        taps.offset[at] = taps.weight.size();
        double total = 0.0;
        const std::size_t begin = taps.weight.size();
        for (int j = first; j <= last; ++j) {
            const double w = std::max(0.0, 1.0 - std::abs(j - centre) / radius);
            taps.weight.push_back(static_cast<float>(w));
            total += w;
        }
        for (std::size_t k = begin; k < taps.weight.size(); ++k)
            taps.weight[k] = static_cast<float>(taps.weight[k] / total);
        lowest = std::min(lowest, std::clamp(first, 0, samples - 1));
        highest = std::max(highest, std::clamp(last, 0, samples - 1));
    }
    taps.lowest = lowest;
    taps.highest = std::max(lowest, highest);
    return taps;
}

}  // namespace

DeviceBox united(const DeviceBox& a, const DeviceBox& b) {
    const int left = std::min(a.x, b.x);
    const int top = std::min(a.y, b.y);
    return {left, top, std::max(a.x + a.width, b.x + b.width) - left,
            std::max(a.y + a.height, b.y + b.height) - top};
}

PixelGrid cropped(const PixelGrid& grid, const DeviceBox& outer, const DeviceBox& part) {
    PixelGrid out;
    out.width = std::max(0, part.width);
    out.height = std::max(0, part.height);
    out.pixels.resize(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height));
    for (int y = 0; y < out.height; ++y) {
        const auto from = grid.pixels.begin() +
                          static_cast<std::ptrdiff_t>(grid_index(part.x - outer.x, part.y - outer.y + y, grid.width));
        std::copy(from, from + out.width, out.pixels.begin() + static_cast<std::ptrdiff_t>(grid_index(0, y, out.width)));
    }
    return out;
}

PixelGrid box_reduced(const PixelBuffer& source, RectI area, int factor) {
    PixelGrid grid;
    grid.width = ceil_div(area.width, factor);
    grid.height = ceil_div(area.height, factor);
    grid.pixels.resize(static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height));
    if (factor == 1) {
        parallel_for(static_cast<std::size_t>(grid.height), [&](std::size_t row_index) {
            const auto row = source.row(area.y + static_cast<int>(row_index))
                                 .subspan(static_cast<std::size_t>(area.x), static_cast<std::size_t>(area.width));
            std::copy(row.begin(), row.end(), grid.pixels.begin() + static_cast<std::ptrdiff_t>(row_index * grid.width));
        });
        return grid;
    }
    parallel_for(static_cast<std::size_t>(grid.height), [&](std::size_t row_index) {
        const int y = static_cast<int>(row_index);
        const int y_end = std::min((y + 1) * factor, area.height);
        for (int x = 0; x < grid.width; ++x) {
            const int x_end = std::min((x + 1) * factor, area.width);
            std::uint32_t red = 0, green = 0, blue = 0, alpha = 0, count = 0;
            for (int sy = y * factor; sy < y_end; ++sy) {
                const auto row = source.row(area.y + sy);
                for (int sx = x * factor; sx < x_end; ++sx) {
                    const Rgba8 pixel = row[static_cast<std::size_t>(area.x + sx)];
                    red += pixel.r * pixel.a;
                    green += pixel.g * pixel.a;
                    blue += pixel.b * pixel.a;
                    alpha += pixel.a;
                    ++count;
                }
            }
            Rgba8& out = grid.pixels[grid_index(x, y, grid.width)];
            const std::uint32_t half = alpha / 2;
            out.r = alpha == 0 ? 0 : static_cast<std::uint8_t>((red + half) / alpha);
            out.g = alpha == 0 ? 0 : static_cast<std::uint8_t>((green + half) / alpha);
            out.b = alpha == 0 ? 0 : static_cast<std::uint8_t>((blue + half) / alpha);
            out.a = static_cast<std::uint8_t>((alpha + count / 2) / count);
        }
    });
    return grid;
}

int quick_level(double scale) {
    if (scale >= 1.0) return 0;
    return std::clamp(static_cast<int>(std::floor(-std::log2(scale) + 0.5)), 0, kMaxQuickLevel);
}

int resample_level(double scale) {
    if (scale >= 0.5) return 0;
    return std::max(0, static_cast<int>(std::floor(-std::log2(scale))) - 1);
}

float resample_overlap(double scale) { return static_cast<float>(2.0 / scale); }

float tile_edge(float doc, float zoom, float dpr, int extent) {
    const auto size = static_cast<float>(extent);
    return doc >= size ? size * zoom * dpr : std::round(doc * zoom * dpr);
}

DeviceBox tile_device_box(int column, int row, int tile_size, float overlap, float zoom, float dpr,
                          int canvas_width, int canvas_height) {
    const auto span = [&](int index, int extent) {
        const float near = tile_edge(static_cast<float>(index * tile_size), zoom, dpr, extent);
        const float far = tile_edge(static_cast<float>((index + 1) * tile_size) + overlap, zoom, dpr, extent);
        return std::pair{round_to_int(near), std::max(1, round_to_int(far - near))};
    };
    const auto [x, width] = span(column, canvas_width);
    const auto [y, height] = span(row, canvas_height);
    return {x, y, width, height};
}

PixelGrid resampled(const PixelBuffer& source, DeviceBox box, double scale) {
    PixelGrid out;
    out.width = std::max(0, box.width);
    out.height = std::max(0, box.height);
    out.pixels.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height),
                      kTransparent);
    if (out.pixels.empty() || source.width() <= 0 || source.height() <= 0) return out;

    const int factor = 1 << resample_level(scale);
    const double radius = 1.0 / (scale * factor);
    const int mip_width = ceil_div(source.width(), factor);
    const int mip_height = ceil_div(source.height(), factor);
    const AxisTaps columns = axis_taps(box.x, out.width, scale, factor, radius, mip_width);
    const AxisTaps rows = axis_taps(box.y, out.height, scale, factor, radius, mip_height);

    // Only the samples the box reads are averaged down, on factor boundaries so
    // every tile sees the same ones.
    const int left = columns.lowest * factor;
    const int top = rows.lowest * factor;
    const RectI area{left, top, std::min((columns.highest + 1) * factor, source.width()) - left,
                     std::min((rows.highest + 1) * factor, source.height()) - top};
    const PixelGrid mip = box_reduced(source, area, factor);

    // Colour is weighed by alpha while filtering, then divided back out, so
    // transparent samples neither darken nor tint what they border.
    struct Weighed {
        float red = 0, green = 0, blue = 0, alpha = 0;

        void add(float weight, const Weighed& other) {
            red += weight * other.red;
            green += weight * other.green;
            blue += weight * other.blue;
            alpha += weight * other.alpha;
        }
    };
    const auto sample_at = [&](int x, int y) {
        const Rgba8 pixel = mip.pixels[grid_index(x, y, mip.width)];
        const float alpha = static_cast<float>(pixel.a) * (1.0f / kChannelMaxFloat);
        return Weighed{static_cast<float>(pixel.r) * alpha, static_cast<float>(pixel.g) * alpha,
                       static_cast<float>(pixel.b) * alpha, alpha};
    };

    std::vector<Weighed> across(static_cast<std::size_t>(mip.height) * static_cast<std::size_t>(out.width));
    parallel_for(static_cast<std::size_t>(mip.height), [&](std::size_t row_index) {
        const int y = static_cast<int>(row_index);
        for (int x = 0; x < out.width; ++x) {
            const auto at = static_cast<std::size_t>(x);
            Weighed sum;
            for (int k = 0; k < columns.count[at]; ++k) {
                const int index = std::clamp(columns.first[at] + k, 0, mip_width - 1) - columns.lowest;
                const float w = columns.weight[columns.offset[at] + static_cast<std::size_t>(k)];
                sum.add(w, sample_at(index, y));
            }
            across[grid_index(x, y, out.width)] = sum;
        }
    });

    const auto byte_of = [](float value) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, kChannelMaxFloat)));
    };
    parallel_for(static_cast<std::size_t>(out.height), [&](std::size_t row_index) {
        const int y = static_cast<int>(row_index);
        const auto at = row_index;
        for (int x = 0; x < out.width; ++x) {
            Weighed sum;
            for (int k = 0; k < rows.count[at]; ++k) {
                const int index = std::clamp(rows.first[at] + k, 0, mip_height - 1) - rows.lowest;
                const float w = rows.weight[rows.offset[at] + static_cast<std::size_t>(k)];
                sum.add(w, across[grid_index(x, index, out.width)]);
            }
            Rgba8& pixel = out.pixels[grid_index(x, y, out.width)];
            if (sum.alpha <= 0.0f) continue;
            pixel = Rgba8{byte_of(sum.red / sum.alpha), byte_of(sum.green / sum.alpha),
                          byte_of(sum.blue / sum.alpha), byte_of(sum.alpha * kChannelMaxFloat)};
        }
    });
    return out;
}

PixelGrid sampled(const PixelBuffer& source, DeviceBox box, double scale, int level) {
    PixelGrid out;
    out.width = std::max(0, box.width);
    out.height = std::max(0, box.height);
    out.pixels.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height),
                      kTransparent);
    if (out.pixels.empty() || source.width() <= 0 || source.height() <= 0) return out;

    const int factor = 1 << std::max(0, level);
    const int mip_width = ceil_div(source.width(), factor);
    const int mip_height = ceil_div(source.height(), factor);
    const auto indices = [&](int start, int length, int limit) {
        std::vector<int> result(static_cast<std::size_t>(length));
        for (int i = 0; i < length; ++i) {
            const double centre = (start + i + 0.5) / scale / factor;
            result[static_cast<std::size_t>(i)] = std::clamp(static_cast<int>(std::floor(centre)), 0, limit - 1);
        }
        return result;
    };
    const std::vector<int> columns = indices(box.x, out.width, mip_width);
    const std::vector<int> rows = indices(box.y, out.height, mip_height);

    const int left = columns.front();
    const int top = rows.front();
    const RectI area{left * factor, top * factor,
                     std::min((columns.back() + 1) * factor, source.width()) - left * factor,
                     std::min((rows.back() + 1) * factor, source.height()) - top * factor};
    const PixelGrid mip = factor == 1 ? PixelGrid{} : box_reduced(source, area, factor);

    for (int y = 0; y < out.height; ++y) {
        const std::size_t base = grid_index(0, y, out.width);
        const int row = rows[static_cast<std::size_t>(y)];
        if (factor == 1) {
            const auto line = source.row(row);
            for (int x = 0; x < out.width; ++x)
                out.pixels[base + static_cast<std::size_t>(x)] =
                    line[static_cast<std::size_t>(columns[static_cast<std::size_t>(x)])];
        } else {
            for (int x = 0; x < out.width; ++x)
                out.pixels[base + static_cast<std::size_t>(x)] =
                    mip.pixels[grid_index(columns[static_cast<std::size_t>(x)] - left, row - top, mip.width)];
        }
    }
    return out;
}

}  // namespace lsp
