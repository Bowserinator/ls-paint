#include "brushes/brush.hpp"

namespace lsp {

BrushProfile brush::profile(BrushKind kind) {
    BrushProfile profile;
    switch (kind) {
        case BrushKind::Brush: break;  // the plain brush follows the tool options
        case BrushKind::Calligraphy1:
            profile.angle = 45.0f;
            profile.nib_width = 0.3f;
            profile.spacing = 0.08f;
            break;
        case BrushKind::Calligraphy2:
            profile.angle = -45.0f;
            profile.nib_width = 0.3f;
            profile.spacing = 0.08f;
            break;
        case BrushKind::Airbrush:
            profile.deposit = Deposit::Spray;
            profile.alpha = 0.55f;
            profile.spacing = 0.1f;
            break;
        case BrushKind::Oil:
            profile.shape = BrushShape::Square;
            profile.nib_width = 0.4f;
            profile.spacing = 0.1f;
            profile.coverage = 0.9f;
            profile.alpha = 0.93f;
            profile.flat = true;
            profile.follows_stroke = true;
            profile.bristle = 0.6f;
            profile.ragged = 0.45f;
            profile.tint = 0.12f;
            profile.pickup = 0.004f;
            break;
        case BrushKind::Crayon:
            profile.deposit = Deposit::Waxy;
            profile.coverage = 0.8f;
            profile.alpha = 0.95f;
            profile.grain = 1.0f;
            profile.spacing = 0.15f;
            profile.flat = true;
            break;
        case BrushKind::Marker:
            profile.shape = BrushShape::Square;
            profile.coverage = 0.96f;
            profile.alpha = 0.55f;
            profile.spacing = 0.1f;
            profile.blend = BlendMode::Multiply;
            profile.flat = true;
            break;
        case BrushKind::NaturalPencil:
            profile.deposit = Deposit::Graphite;
            profile.coverage = 0.45f;
            profile.alpha = 0.8f;
            profile.grain = 0.5f;
            profile.spacing = 0.12f;
            profile.flat = true;
            break;
        case BrushKind::Watercolor:
            profile.shape = BrushShape::Square;
            profile.nib_width = 0.5f;
            profile.spacing = 0.1f;
            profile.coverage = 0.55f;
            profile.alpha = 0.5f;
            profile.flat = true;
            profile.follows_stroke = true;
            profile.bristle = 0.35f;
            profile.ragged = 0.3f;
            profile.pickup = 0.02f;
            profile.wet_edge = 0.45f;
            break;
        case BrushKind::Blur: profile.coverage = 0.6f; break;
        case BrushKind::Liquify:
            profile.coverage = 0.45f;
            profile.spacing = 0.06f;
            break;
    }
    return profile;
}

}  // namespace lsp
