#pragma once

#include <array>
#include <cstddef>

#include "core/types.hpp"

namespace lsp {

inline constexpr int kDefaultCanvasWidth = 800;
inline constexpr int kDefaultCanvasHeight = 600;
inline constexpr int kMaxCanvasDimension = 4096;
inline constexpr std::size_t kMaxHistoryEntries = 200;
inline constexpr int kCustomColorSlots = 10;

inline constexpr std::array<Rgba8, 20> kDefaultPalette{{
    {0, 0, 0},       {120, 120, 120}, {130, 0, 0},     {255, 0, 0},     {255, 98, 0},
    {255, 225, 0},   {0, 156, 42},    {0, 119, 255},   {0, 3, 207},     {134, 0, 207},
    {255, 255, 255}, {200, 200, 200}, {156, 88, 0},    {255, 117, 117}, {255, 194, 64},
    {255, 219, 140}, {94, 255, 100},  {171, 209, 255}, {102, 94, 255},  {153, 94, 255},
}};

}  // namespace lsp
