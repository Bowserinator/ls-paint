#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "core/types.hpp"

namespace lsp {

inline constexpr int kDefaultCanvasWidth = 800;
inline constexpr int kDefaultCanvasHeight = 600;
// Images larger than this are refused on load and never produced by growth.
inline constexpr int kMaxImageDimension = 16384;
inline constexpr int kMaxCanvasDimension = kMaxImageDimension;

// Pixel storage is split into bands of whole rows holding about this many
// pixels (rounded down to a power of two rows). A band is the unit that copies
// of an image share until one of them is written to.
inline constexpr std::size_t kPixelBandPixels = 1u << 20;
// Undo entries keep the changed pixels in squares of this many pixels a side.
inline constexpr int kUndoTileSize = 128;
// The blur brush computes the blurred image in squares of this many pixels a
// side, only where the brush reaches.
inline constexpr int kBlurChunkSize = 256;
// A stroke that lays down ink once per pixel tracks its coverage in squares of
// this many pixels a side, only where it has been.
inline constexpr int kCoverageChunkSize = 256;
// The canvas is shown as squares of this many pixels a side, so a brush stroke
// only refreshes the squares it touched.
inline constexpr int kDisplayTileSize = 512;
// How long the zoom must hold still before the canvas is resampled exactly.
inline constexpr int kZoomSettleMilliseconds = 120;

// The application's name, which is also the name of its config folder.
inline constexpr std::string_view kAppId = "ls_paint";
inline constexpr std::size_t kMaxHistoryEntries = 20;
// The history also keeps a size budget, because one entry can hold a whole
// canvas.
inline constexpr std::size_t kMaxHistoryBytes = 512u * 1024u * 1024u;
// The custom colors in the ribbon's palette.
inline constexpr int kCustomColorSlots = 10;
// The number of files the File menu's recent list remembers.
inline constexpr std::size_t kMaxRecentFiles = 10;
// The custom colors in the Edit Colors dialog, two rows of eight.
inline constexpr int kDialogCustomColorSlots = 16;

// Extensions of the image files the application reads and writes.
inline constexpr std::array<std::string_view, 4> kImageExtensions{".png", ".bmp", ".jpg", ".jpeg"};

inline constexpr std::array<Rgba8, 20> kDefaultPalette{{
    {0, 0, 0},       {120, 120, 120}, {130, 0, 0},     {255, 0, 0},     {255, 98, 0},
    {255, 225, 0},   {0, 156, 42},    {0, 119, 255},   {0, 3, 207},     {134, 0, 207},
    {255, 255, 255}, {200, 200, 200}, {156, 88, 0},    {255, 117, 117}, {255, 194, 64},
    {255, 219, 140}, {94, 255, 100},  {171, 209, 255}, {102, 94, 255},  {153, 94, 255},
}};

}  // namespace lsp
