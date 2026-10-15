#include "tools/text_tool.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <utility>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "core/raster.hpp"
#include "tools/tool_context.hpp"

#ifndef LSP_FONT_DIR
#define LSP_FONT_DIR ""
#endif

namespace lsp {
namespace {

constexpr float kItalicSlant = 0.21f;  // tan(12 degrees): the shear of a faux italic

// The bundled faces. One file per family: bold and italic are synthesized, so
// shipping a family costs a single file.
constexpr std::pair<std::string_view, std::string_view> kFamilies[]{
    {"DejaVu Sans", "DejaVuSans.ttf"},
    {"DejaVu Serif", "DejaVuSerif.ttf"},
    {"DejaVu Sans Mono", "DejaVuSansMono.ttf"},
};

struct LoadedFont {
    std::vector<unsigned char> bytes;
    stbtt_fontinfo info{};
    bool ready = false;
};

// Loads the face for `family`, falling back to the first bundled file. The
// result is cached, including the failure, so a missing directory warns once.
const LoadedFont* load_font(std::string_view family) {
    static std::map<std::string, LoadedFont> cache;
    const std::string key(family);
    if (const auto found = cache.find(key); found != cache.end()) return found->second.ready ? &found->second : nullptr;

    LoadedFont font;
    std::string file;
    for (const auto& face : kFamilies) {
        if (face.first == family) file = face.second;
    }
    if (file.empty()) file = kFamilies[0].second;

    std::ifstream stream{std::string(LSP_FONT_DIR) + "/" + file, std::ios::binary};
    if (stream) {
        font.bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        if (!font.bytes.empty()) {
            const int offset = stbtt_GetFontOffsetForIndex(font.bytes.data(), 0);
            font.ready = offset >= 0 && stbtt_InitFont(&font.info, font.bytes.data(), offset) != 0;
        }
    }
    if (!font.ready) std::fprintf(stderr, "No usable font file %s in %s\n", file.c_str(), LSP_FONT_DIR);

    auto& stored = cache[key] = std::move(font);
    return stored.ready ? &stored : nullptr;
}

// Steps past one UTF-8 sequence and returns its codepoint. Malformed bytes
// decode as the replacement character.
std::uint32_t next_codepoint(std::string_view text, std::size_t& index) {
    const auto lead = static_cast<unsigned char>(text[index++]);
    if (lead < 0x80) return lead;

    int continuation = 0;
    std::uint32_t codepoint = 0;
    if ((lead & 0xE0) == 0xC0) {
        continuation = 1;
        codepoint = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
        continuation = 2;
        codepoint = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
        continuation = 3;
        codepoint = lead & 0x07;
    } else {
        return 0xFFFD;
    }
    if (index + static_cast<std::size_t>(continuation) > text.size()) {
        index = text.size();
        return 0xFFFD;
    }
    for (int i = 0; i < continuation; ++i) {
        const auto byte = static_cast<unsigned char>(text[index]);
        if ((byte & 0xC0) != 0x80) return 0xFFFD;
        codepoint = (codepoint << 6) | (byte & 0x3F);
        ++index;
    }
    return codepoint;
}

// One glyph, ready to stamp: the coverage bitmap plus the box it covers
// relative to the pen and the baseline.
struct GlyphBitmap {
    std::vector<std::uint8_t> coverage;
    int width = 0;
    int height = 0;
    int x = 0;
    int y = 0;
};

// Applies the faux italic shear and bold dilation to a glyph's coverage. The
// sheer shifts a row further right the higher it sits above the baseline, and
// the dilation widens every row to the right, so both grow the bitmap.
GlyphBitmap shape_glyph(const unsigned char* raw, int width, int height, int x_offset, int y_offset,
                        const TextOptions& options) {
    GlyphBitmap glyph;
    const int shear =
        options.italic ? std::max(0, static_cast<int>(std::lround(-y_offset * kItalicSlant))) : 0;
    const int passes = options.bold ? std::max(1, static_cast<int>(std::lround(options.size / 16.0f))) : 0;
    glyph.width = width + shear + passes;
    glyph.height = height;
    glyph.x = x_offset;
    glyph.y = y_offset;
    if (glyph.width <= 0 || height <= 0) {
        glyph.width = 0;
        return glyph;
    }

    glyph.coverage.assign(static_cast<std::size_t>(glyph.width) * height, 0);
    for (int row = 0; row < height; ++row) {
        const int shift = options.italic
                              ? std::max(0, static_cast<int>(std::lround(-(y_offset + row) * kItalicSlant)))
                              : 0;
        for (int column = 0; column < width; ++column) {
            glyph.coverage[static_cast<std::size_t>(row) * glyph.width + column + shift] =
                raw[static_cast<std::size_t>(row) * width + column];
        }
    }
    if (passes > 0) {
        const std::vector<std::uint8_t> sheared = glyph.coverage;
        for (int row = 0; row < height; ++row) {
            for (int column = 0; column < width + shear; ++column) {
                const std::uint8_t value = sheared[static_cast<std::size_t>(row) * glyph.width + column];
                for (int step = 1; step <= passes; ++step) {
                    auto& target = glyph.coverage[static_cast<std::size_t>(row) * glyph.width + column + step];
                    target = std::max(target, value);
                }
            }
        }
    }
    return glyph;
}

}  // namespace

RectI draw_text(raster::Target target, Point2f origin, std::string_view text,
                const TextOptions& options, Rgba8 color) {
    const LoadedFont* font = load_font(options.family);
    if (font == nullptr || text.empty() || options.size <= 0) return {};

    const float scale = stbtt_ScaleForPixelHeight(&font->info, options.size);
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &line_gap);
    const int line_height =
        std::max(1, static_cast<int>(std::lround((ascent - descent + line_gap) * scale)));

    // The press marks the top left of the first line box.
    const int left = static_cast<int>(std::lround(origin.x));
    int pen_x = left;
    int pen_y = static_cast<int>(std::lround(origin.y + ascent * scale));

    RectI touched;
    int previous = 0;
    std::size_t index = 0;
    while (index < text.size()) {
        const std::uint32_t codepoint = next_codepoint(text, index);
        if (codepoint == '\n') {
            pen_x = left;
            pen_y += line_height;
            previous = 0;
            continue;
        }

        int advance = 0;
        int left_side_bearing = 0;
        stbtt_GetCodepointHMetrics(&font->info, static_cast<int>(codepoint), &advance,
                                   &left_side_bearing);
        if (previous != 0) {
            pen_x += static_cast<int>(
                std::lround(stbtt_GetCodepointKernAdvance(&font->info, previous,
                                                          static_cast<int>(codepoint)) *
                            scale));
        }

        int width = 0;
        int height = 0;
        int x_offset = 0;
        int y_offset = 0;
        unsigned char* raw = stbtt_GetCodepointBitmap(&font->info, scale, scale,
                                                      static_cast<int>(codepoint), &width, &height,
                                                      &x_offset, &y_offset);
        if (raw != nullptr) {
            const GlyphBitmap glyph =
                shape_glyph(raw, width, height, x_offset, y_offset, options);
            if (glyph.width > 0 && glyph.height > 0) {
                const RectI box{pen_x + glyph.x, pen_y + glyph.y, glyph.width, glyph.height};
                raster::draw_glyph(target, box.x, box.y, glyph.width, glyph.height, glyph.width,
                                   glyph.coverage.data(), color);
                touched = touched.united(box);
            }
            stbtt_FreeBitmap(raw, nullptr);
        }

        pen_x += static_cast<int>(std::lround(advance * scale));
        previous = static_cast<int>(codepoint);
    }
    return touched.intersected(target.pixels.bounds());
}

void TextTool::on_pointer_down(ToolContext& context, Point2f position, PointerButton button) {
    TextOverlayState& text = context.ui.text;
    text.origin = position;
    text.color = context.colors.ink(button);
    text.visible = true;
}

void TextTool::on_deactivate(ToolContext& context) { context.ui.text = {}; }

}  // namespace lsp
