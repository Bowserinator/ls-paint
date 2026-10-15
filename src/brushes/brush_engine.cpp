#include "brushes/brush_engine.hpp"

#include <algorithm>
#include <cmath>

namespace lsp {

RectI BrushEngine::stroke_to(PixelBuffer& target, Point2f to) {
    const float step = std::max(1.0f, m_brush.spacing * static_cast<float>(m_brush.size));

    if (!m_last) {
        m_last = to;
        m_carry = step;
        return stamp(target, to);
    }

    const Point2f from = *m_last;
    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float length = std::hypot(dx, dy);

    RectI dirty;
    float travelled = m_carry;
    for (; travelled <= length; travelled += step) {
        const float t = travelled / length;
        dirty = dirty.united(stamp(target, {from.x + dx * t, from.y + dy * t}));
    }

    m_carry = travelled - length;
    m_last = to;
    return dirty;
}

RectI BrushEngine::stamp(PixelBuffer& target, Point2f center) const {
    const int size = std::max(1, m_brush.size);
    const float radius = static_cast<float>(size) * 0.5f;
    const bool odd = size % 2 == 1;
    const float cx = odd ? std::floor(center.x) + 0.5f : std::round(center.x);
    const float cy = odd ? std::floor(center.y) + 0.5f : std::round(center.y);

    const int left = static_cast<int>(std::floor(cx - radius));
    const int top = static_cast<int>(std::floor(cy - radius));
    const RectI area = RectI{left, top, size + 1, size + 1}.intersected(target.bounds());

    for (int y = area.y; y < area.bottom(); ++y) {
        for (int x = area.x; x < area.right(); ++x) {
            const float dx = static_cast<float>(x) + 0.5f - cx;
            const float dy = static_cast<float>(y) + 0.5f - cy;
            const bool inside = m_brush.shape == BrushShape::Round
                                    ? dx * dx + dy * dy <= radius * radius
                                    : std::abs(dx) <= radius && std::abs(dy) <= radius;
            if (inside && m_selection.contains(x, y)) target.at(x, y) = m_color;
        }
    }
    return area;
}

}  // namespace lsp
