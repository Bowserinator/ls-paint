#include "core/pixel_buffer.hpp"

#include <algorithm>
#include <unordered_set>

#include "config.hpp"
#include "utils/parallel.hpp"

namespace lsp {

PixelBuffer::PixelBuffer(int width, int height, Rgba8 fill) : m_width(width), m_height(height) {
    if (width <= 0 || height <= 0) {
        m_width = m_height = 0;
        return;
    }

    const auto rows_per_band = std::max<std::size_t>(1, kPixelBandPixels / static_cast<std::size_t>(width));
    const auto band_rows = std::bit_floor(rows_per_band);
    m_band_shift = static_cast<unsigned>(std::countr_zero(band_rows));
    m_band_mask = band_rows - 1;

    const auto row_count = static_cast<std::size_t>(height);
    const std::size_t band_count = (row_count + band_rows - 1) / band_rows;
    const auto make_band = [&](std::size_t rows) {
        return std::make_shared<std::vector<Rgba8>>(rows * static_cast<std::size_t>(width), fill);
    };

    // Every full band starts out as the same storage and is split off when
    // written to.
    m_bands.reserve(band_count);
    const Band full = make_band(std::min(band_rows, row_count));
    for (std::size_t i = 0; i < band_count; ++i) {
        const std::size_t rows = std::min(band_rows, row_count - i * band_rows);
        m_bands.push_back(rows == band_rows ? full : make_band(rows));
    }
}

bool operator==(const PixelBuffer& a, const PixelBuffer& b) {
    if (a.m_width != b.m_width || a.m_height != b.m_height) return false;
    for (int y = 0; y < a.m_height; ++y) {
        if (a.row_shared_with(b, y)) continue;
        if (!std::ranges::equal(a.row(y), b.row(y))) return false;
    }
    return true;
}

bool PixelBuffer::row_shared_with(const PixelBuffer& other, int y) const noexcept {
    if (m_width != other.m_width || m_band_shift != other.m_band_shift) return false;
    const auto band = static_cast<std::size_t>(y) >> m_band_shift;
    return m_bands[band] == other.m_bands[band];
}

std::size_t PixelBuffer::storage_bytes() const noexcept {
    std::unordered_set<const void*> seen;
    std::size_t total = 0;
    for (const Band& band : m_bands)
        if (seen.insert(band.get()).second) total += band->size() * sizeof(Rgba8);
    return total;
}

std::vector<Rgba8> PixelBuffer::contiguous_copy(RectI region) const {
    region = region.intersected(bounds());
    std::vector<Rgba8> result;
    result.reserve(static_cast<std::size_t>(region.width) * static_cast<std::size_t>(region.height));
    for (int y = region.y; y < region.bottom(); ++y) {
        const auto source = row(y).subspan(static_cast<std::size_t>(region.x),
                                           static_cast<std::size_t>(region.width));
        result.insert(result.end(), source.begin(), source.end());
    }
    return result;
}

PixelBuffer PixelBuffer::copy_region(RectI region) const {
    region = region.intersected(bounds());
    PixelBuffer result(region.width, region.height);
    for (int y = 0; y < region.height; ++y) {
        const auto source = row(region.y + y).subspan(static_cast<std::size_t>(region.x),
                                                      static_cast<std::size_t>(region.width));
        std::ranges::copy(source, result.row(y).begin());
    }
    return result;
}

void PixelBuffer::blit(int x, int y, const PixelBuffer& source) {
    const RectI area = RectI{x, y, source.width(), source.height()}.intersected(bounds());
    for (int target_y = area.y; target_y < area.bottom(); ++target_y) {
        const auto pixels = source.row(target_y - y).subspan(static_cast<std::size_t>(area.x - x),
                                                             static_cast<std::size_t>(area.width));
        std::ranges::copy(pixels, row(target_y).begin() + area.x);
    }
}

void PixelBuffer::copy_region_from(const PixelBuffer& source, RectI region) {
    if (source.m_width != m_width || source.m_height != m_height) return;
    region = region.intersected(bounds());
    for (int y = region.y; y < region.bottom(); ++y) {
        if (row_shared_with(source, y)) continue;
        const auto pixels = source.row(y).subspan(static_cast<std::size_t>(region.x),
                                                  static_cast<std::size_t>(region.width));
        std::ranges::copy(pixels, row(y).begin() + region.x);
    }
}

RectI PixelBuffer::band_rows(int index) const noexcept {
    const int rows = 1 << m_band_shift;
    const int first = index * rows;
    return {0, first, m_width, std::min(rows, m_height - first)};
}

void PixelBuffer::allocate_all_bands() {
    for (std::size_t i = 0; i < m_bands.size(); ++i) {
        const RectI rows = band_rows(static_cast<int>(i));
        m_bands[i] = std::make_shared<std::vector<Rgba8>>(static_cast<std::size_t>(rows.height) *
                                                          static_cast<std::size_t>(m_width));
    }
}

namespace {

// Edge of the squares a transpose walks, so both the reads and the writes stay
// within a few cache lines at a time.
constexpr int kTransposeBlock = 32;

}  // namespace

// Both 90 degree turns are a transpose with one axis reversed. The result is
// built a band at a time, each on its own thread, and only ever written once.
PixelBuffer PixelBuffer::rotated_90_cw() const {
    PixelBuffer result(m_height, m_width);
    result.allocate_all_bands();
    parallel_for(static_cast<std::size_t>(result.band_count()), [&](std::size_t index) {
        const RectI rows = result.band_rows(static_cast<int>(index));
        for (int r0 = rows.y; r0 < rows.bottom(); r0 += kTransposeBlock) {
            const int r1 = std::min(r0 + kTransposeBlock, rows.bottom());
            for (int y0 = 0; y0 < m_height; y0 += kTransposeBlock) {
                const int y1 = std::min(y0 + kTransposeBlock, m_height);
                for (int r = r0; r < r1; ++r) {
                    Rgba8* out = result.band(static_cast<int>(index))->data() +
                                 static_cast<std::size_t>(r - rows.y) * static_cast<std::size_t>(m_height);
                    for (int y = y0; y < y1; ++y) out[m_height - 1 - y] = row(y)[static_cast<std::size_t>(r)];
                }
            }
        }
    });
    return result;
}

PixelBuffer PixelBuffer::rotated_90_ccw() const {
    PixelBuffer result(m_height, m_width);
    result.allocate_all_bands();
    parallel_for(static_cast<std::size_t>(result.band_count()), [&](std::size_t index) {
        const RectI rows = result.band_rows(static_cast<int>(index));
        for (int r0 = rows.y; r0 < rows.bottom(); r0 += kTransposeBlock) {
            const int r1 = std::min(r0 + kTransposeBlock, rows.bottom());
            for (int y0 = 0; y0 < m_height; y0 += kTransposeBlock) {
                const int y1 = std::min(y0 + kTransposeBlock, m_height);
                for (int r = r0; r < r1; ++r) {
                    Rgba8* out = result.band(static_cast<int>(index))->data() +
                                 static_cast<std::size_t>(r - rows.y) * static_cast<std::size_t>(m_height);
                    for (int y = y0; y < y1; ++y) out[y] = row(y)[static_cast<std::size_t>(m_width - 1 - r)];
                }
            }
        }
    });
    return result;
}

PixelBuffer PixelBuffer::rotated_180() const {
    PixelBuffer result(m_width, m_height);
    for (int y = 0; y < m_height; ++y)
        std::ranges::reverse_copy(row(y), result.row(m_height - 1 - y).begin());
    return result;
}

PixelBuffer PixelBuffer::flipped_horizontal() const {
    PixelBuffer result(m_width, m_height);
    for (int y = 0; y < m_height; ++y) std::ranges::reverse_copy(row(y), result.row(y).begin());
    return result;
}

PixelBuffer PixelBuffer::flipped_vertical() const {
    PixelBuffer result(m_width, m_height);
    for (int y = 0; y < m_height; ++y)
        std::ranges::copy(row(y), result.row(m_height - 1 - y).begin());
    return result;
}

namespace {

// How far a line `position` along an axis of `length` lines slides: the line
// at the start stays put for a positive slope and the line at the end does for
// a negative one.
int skew_shift(float slope, int position, int length) {
    return round_to_int(std::abs(slope) * static_cast<float>(slope >= 0.0f ? position : length - 1 - position));
}

}  // namespace

PixelBuffer PixelBuffer::scaled(int width, int height) const {
    if (width <= 0 || height <= 0 || m_width <= 0 || m_height <= 0) return {};
    PixelBuffer result(width, height);
    for (int y = 0; y < height; ++y) {
        const int source_y = std::min(m_height - 1, static_cast<int>(static_cast<long long>(y) * m_height / height));
        const std::span<const Rgba8> source = row(source_y);
        const std::span<Rgba8> out = result.row(y);
        for (int x = 0; x < width; ++x) {
            const int source_x = std::min(m_width - 1, static_cast<int>(static_cast<long long>(x) * m_width / width));
            out[static_cast<std::size_t>(x)] = source[static_cast<std::size_t>(source_x)];
        }
    }
    return result;
}

PixelBuffer PixelBuffer::skewed_horizontal(float degrees, Rgba8 fill) const {
    if (m_width <= 0 || m_height <= 0) return {};
    const float slope = std::tan(radians_from_degrees(degrees));
    const int room = std::max(skew_shift(slope, 0, m_height), skew_shift(slope, m_height - 1, m_height));
    PixelBuffer result(m_width + room, m_height, fill);
    for (int y = 0; y < m_height; ++y)
        std::ranges::copy(row(y), result.row(y).begin() + skew_shift(slope, y, m_height));
    return result;
}

PixelBuffer PixelBuffer::skewed_vertical(float degrees, Rgba8 fill) const {
    if (m_width <= 0 || m_height <= 0) return {};
    const float slope = std::tan(radians_from_degrees(degrees));
    const int room = std::max(skew_shift(slope, 0, m_width), skew_shift(slope, m_width - 1, m_width));
    PixelBuffer result(m_width, m_height + room, fill);
    for (int y = 0; y < m_height; ++y) {
        const std::span<const Rgba8> source = row(y);
        for (int x = 0; x < m_width; ++x)
            result.at(x, y + skew_shift(slope, x, m_width)) = source[static_cast<std::size_t>(x)];
    }
    return result;
}

}  // namespace lsp
