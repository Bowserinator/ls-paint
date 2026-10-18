#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "core/types.hpp"

namespace lsp {

inline constexpr int kDefaultCanvasWidth = 800;
inline constexpr int kDefaultCanvasHeight = 600;
inline constexpr int kMaxCanvasDimension = 4096;
// Images larger than this are refused on load and never produced by growth.
inline constexpr int kMaxImageDimension = 16384;

// The application's own name: the folder under the user's config directory that
// the settings database lives in.
inline constexpr std::string_view kAPP_ID = "ls_paint";
inline constexpr std::size_t kMaxHistoryEntries = 20;
// The stack also keeps a size budget, because one entry can hold a whole
// canvas: at 4096x4096 a single geometry entry is 64 MB, so the entry count
// alone would let the history grow into gigabytes.
inline constexpr std::size_t kMaxHistoryBytes = 512u * 1024u * 1024u;
// The custom half of the palette. The ribbon shows the whole palette ten to a
// row, so the default colors and these together fill three rows.
inline constexpr int kCustomColorSlots = 10;
// The File menu's recent list: the openings and savings it remembers.
inline constexpr std::size_t kMaxRecentFiles = 10;
// The grid of custom colors the Edit Colors dialog edits, two rows of eight.
// OK moves the slot the dialog points at into the ribbon's strip.
inline constexpr int kDialogCustomColorSlots = 16;

// Extensions the file dialogs offer and document_io can write.
inline constexpr std::array<std::string_view, 4> kImageExtensions{".png", ".bmp", ".jpg", ".jpeg"};

inline constexpr std::array<Rgba8, 20> kDefaultPalette{{
    {0, 0, 0},       {120, 120, 120}, {130, 0, 0},     {255, 0, 0},     {255, 98, 0},
    {255, 225, 0},   {0, 156, 42},    {0, 119, 255},   {0, 3, 207},     {134, 0, 207},
    {255, 255, 255}, {200, 200, 200}, {156, 88, 0},    {255, 117, 117}, {255, 194, 64},
    {255, 219, 140}, {94, 255, 100},  {171, 209, 255}, {102, 94, 255},  {153, 94, 255},
}};

}  // namespace lsp
