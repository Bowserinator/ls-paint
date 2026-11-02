#pragma once

#include <optional>
#include <string_view>

#include "core/blend_modes.hpp"

namespace lsp {

enum class BrushShape { Round, Square };

inline constexpr float kDefaultBrushSpacing = 0.25f;

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
    Blur,
    Liquify,
};

// How a tip puts ink on the pixels it covers.
enum class Deposit {
    Smooth,    // an even layer, faded towards the rim
    Spray,     // scattered droplets, thinning towards the rim
    Waxy,      // pigment that only catches the raised parts of the paper
    Graphite,  // a soft layer that picks up the paper's texture
};

// The nib one brush type lays down: the tip that gets stamped, how much ink
// that tip carries and how closely the stamps sit along a stroke.
struct BrushProfile {
    BrushShape shape = BrushShape::Round;
    float angle = 0;      // degrees, for the chisel tips
    float nib_width = 1;  // across the tip, as a fraction of its length
    float spacing = kDefaultBrushSpacing;
    float coverage = 1;
    float alpha = 1;
    float grain = 0;
    Deposit deposit = Deposit::Smooth;
    BlendMode blend = BlendMode::Normal;
    float feather = 0;   // pixels the soft rim reaches past the tip
    float density = 1;   // chance a droplet lands, for sprayed ink
    bool flat = false;   // a stroke never darkens where it overlaps itself
    bool follows_stroke = false;  // the tip turns to face the way the stroke goes
    float bristle = 0;   // strength of the streaks left along the stroke
    float ragged = 0;    // how unevenly the bristles end
    float tint = 0;      // how much the colour varies from bristle to bristle
    float pickup = 0;    // how readily the paint mixes with the colours below
    float wet_edge = 0;  // extra ink along the sides of the stroke
};

namespace brush {

[[nodiscard]] BrushProfile profile(BrushKind kind);

// The brush type's name for saving, and the type a saved name stands for.
[[nodiscard]] std::string_view name(BrushKind kind);
[[nodiscard]] std::optional<BrushKind> from_name(std::string_view name);

}  // namespace brush

struct Brush {
    int size = 8;
    float spacing = kDefaultBrushSpacing;
    BrushShape shape = BrushShape::Round;
    BrushKind kind = BrushKind::Brush;
};

}  // namespace lsp
