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
            profile.coverage = 0.35f;
            profile.alpha = 0.35f;
            profile.grain = 0.5f;
            profile.spacing = 0.08f;
            break;
        case BrushKind::Oil:
            profile.grain = 0.25f;
            profile.spacing = 0.12f;
            break;
        case BrushKind::Crayon:
            profile.coverage = 0.55f;
            profile.alpha = 0.9f;
            profile.grain = 0.7f;
            profile.spacing = 0.2f;
            break;
        case BrushKind::Marker:
            profile.shape = BrushShape::Square;
            profile.alpha = 0.75f;
            profile.spacing = 0.1f;
            break;
        case BrushKind::NaturalPencil:
            profile.coverage = 0.5f;
            profile.alpha = 0.85f;
            profile.grain = 0.4f;
            profile.spacing = 0.15f;
            break;
        case BrushKind::Watercolor:
            profile.coverage = 0.6f;
            profile.alpha = 0.3f;
            profile.grain = 0.15f;
            profile.spacing = 0.2f;
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
