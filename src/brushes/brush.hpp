#pragma once

namespace lsp {

enum class BrushShape { Round, Square };

// The brush types the Brushes picker offers.
enum class BrushKind {
    Brush,
    Calligraphy1,
    Calligraphy2,
    Airbrush,
    Oil,
    Crayon,
    Marker,
    NaturalPencil,
    Watercolor,
};

// The nib one brush type lays down: the tip that gets stamped, how much ink
// that tip carries and how closely the stamps sit along a stroke.
struct BrushProfile {
    BrushShape shape = BrushShape::Round;
    float angle = 0;      // degrees, for the chisel tips
    float nib_width = 1;  // across the tip, as a fraction of its length
    float spacing = 0.25f;
    float coverage = 1;
    float alpha = 1;
    float grain = 0;
};

namespace brush {

[[nodiscard]] BrushProfile profile(BrushKind kind);

}  // namespace brush

struct Brush {
    int size = 1;
    float spacing = 0.25f;
    BrushShape shape = BrushShape::Round;
    BrushKind kind = BrushKind::Brush;
};

}  // namespace lsp
