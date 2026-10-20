#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "brushes/brush.hpp"
#include "core/pixel_buffer.hpp"
#include "selection/selection_mask.hpp"

namespace lsp {

class BrushEngine {
public:
    BrushEngine(const Brush& brush, Rgba8 color, const SelectionMask& selection,
                const PixelBuffer* source = nullptr)
        : m_brush(brush),
          m_color(color),
          m_selection(selection),
          m_source(source),
          m_profile(effective_profile(brush)),
          m_loaded(color) {}

    RectI stroke_to(PixelBuffer& target, Point2f to);

    // Ends the stroke. A stroke that never travelled far enough to show its
    // direction still leaves a mark.
    RectI finish(PixelBuffer& target);

private:
    // The nib this stroke lays down. The plain brush takes its tip and its
    // stamp spacing from the tool options; every other type brings its own.
    [[nodiscard]] static BrushProfile effective_profile(const Brush& brush);

    // Prepares whatever the sampled kind reads for the whole stroke.
    void prepare_source();

    // Makes sure the blurred image is available over `area`.
    void ensure_blurred(RectI area);
    [[nodiscard]] const Rgba8& blurred_at(int x, int y) const;
    // The ink already laid at a pixel of a canvas `width` by `height`.
    [[nodiscard]] std::uint8_t& coverage_at(int x, int y, int width, int height);

    // Turns the tip towards the way the stroke is heading.
    void steer(float dx, float dy);

    // Stamps the tip at `center`, `path` along the stroke.
    RectI stamp(PixelBuffer& target, Point2f center, float path);

    // Mixes the paint on the tip with the colours it is passing over.
    void pick_up(const PixelBuffer& canvas, float cx, float cy, float radius);

    Brush m_brush;
    Rgba8 m_color;
    const SelectionMask& m_selection;
    const PixelBuffer* m_source = nullptr;  // the layer as the stroke began
    std::vector<PixelBuffer> m_blurred;     // what the blur type samples, by chunk
    int m_blurred_columns = 0;
    bool m_source_ready = false;
    BrushProfile m_profile;
    std::optional<Point2f> m_last;
    Point2f m_start;
    std::optional<Point2f> m_last_stamp;
    float m_carry = 0.0f;
    float m_path = 0.0f;
    Point2f m_dir{1.0f, 0.0f};
    bool m_oriented = false;
    bool m_dab = false;
    std::uint32_t m_seed = 0;
    std::uint32_t m_stamps = 0;
    Rgba8 m_loaded;
    // How much ink this stroke has laid on each pixel, by chunk.
    std::vector<std::vector<std::uint8_t>> m_coverage;
    int m_coverage_columns = 0;
    int m_coverage_rows = 0;
};

}  // namespace lsp
