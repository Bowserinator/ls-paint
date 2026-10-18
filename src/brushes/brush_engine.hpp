#pragma once

#include <optional>

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
          m_profile(effective_profile(brush)) {}

    RectI stroke_to(PixelBuffer& target, Point2f to);

private:
    // The nib this stroke lays down. The plain brush takes its tip and its
    // stamp spacing from the tool options; every other type brings its own.
    [[nodiscard]] static BrushProfile effective_profile(const Brush& brush);

    // The types that change the pixels under the tip instead of laying ink on
    // top of them, so their stamp mixes in what they sampled.
    [[nodiscard]] static bool is_effect(BrushKind kind) noexcept;

    // Prepares whatever the sampled kind reads for the whole stroke.
    void prepare_source();

    // Stamps the tip at `center`, having moved `move` since the last stamp:
    // the pushing kind displaces what it covers by that much.
    RectI stamp(PixelBuffer& target, Point2f center, Point2f move) const;

    Brush m_brush;
    Rgba8 m_color;
    const SelectionMask& m_selection;
    const PixelBuffer* m_source = nullptr;  // the layer as the stroke began
    PixelBuffer m_blurred;                  // what the blur type samples
    bool m_source_ready = false;
    BrushProfile m_profile;
    std::optional<Point2f> m_last;
    Point2f m_start;
    float m_carry = 0.0f;
};

}  // namespace lsp
