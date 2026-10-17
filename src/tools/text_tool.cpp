#include "tools/text_tool.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "core/raster.hpp"
#include "text/font_catalog.hpp"
#include "tools/tool_context.hpp"

#ifndef LSP_FONT_DIR
#define LSP_FONT_DIR ""
#endif

namespace lsp {
namespace {

constexpr float kItalicSlant = 0.21f;  // tan(12 degrees): the shear of a faux italic
// The underline, the strikethrough and the opaque band sit on the line metrics,
// so they follow the size. The offsets are fractions of it.
constexpr float kUnderlineDrop = 0.15f;  // below the baseline
constexpr float kStrikeRise = 0.28f;     // above it, near the middle of the x-height
constexpr float kMarkThickness = 0.06f;

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

// Reads and initializes the face in `path`, false when the file is unusable.
bool read_font(LoadedFont& font, const std::string& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) return false;
    font.bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    if (font.bytes.empty()) return false;
    const int offset = stbtt_GetFontOffsetForIndex(font.bytes.data(), 0);
    font.ready = offset >= 0 && stbtt_InitFont(&font.info, font.bytes.data(), offset) != 0;
    return font.ready;
}

// The file the bundled copy of `family` lives in, empty when no bundled face
// carries that name.
std::string bundled_path_for(std::string_view family) {
    for (const auto& face : kFamilies) {
        if (face.first == family) return std::string(LSP_FONT_DIR) + "/" + std::string(face.second);
    }
    return {};
}

// Loads the face for `family`, preferring the installed file, then a bundled
// face by name and finally the first bundled face. The result is cached,
// including the failure, so a missing directory warns once.
const LoadedFont* load_font(std::string_view family) {
    static std::map<std::string, LoadedFont> cache;
    const std::string key(family);
    if (const auto found = cache.find(key); found != cache.end()) return found->second.ready ? &found->second : nullptr;

    std::vector<std::string> candidates;
    const auto add = [&candidates](std::string path) {
        if (path.empty()) return;
        if (std::ranges::find(candidates, path) != candidates.end()) return;
        candidates.push_back(std::move(path));
    };
    add(installed_fonts().face_path(family));
    add(bundled_path_for(family));
    add(std::string(LSP_FONT_DIR) + "/" + std::string(kFamilies[0].second));

    LoadedFont font;
    for (const std::string& path : candidates) {
        if (read_font(font, path)) break;
    }
    if (!font.ready) std::fprintf(stderr, "No usable font file for %s in %s\n", key.c_str(), LSP_FONT_DIR);

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

std::vector<std::string> text_font_families() {
    std::vector<std::string> families;
    for (const FontFace& face : installed_fonts().faces()) families.push_back(face.family);
    for (const auto& bundled : kFamilies) {
        const bool known = std::ranges::any_of(families, [&bundled](const std::string& family) {
            return same_font_family(family, bundled.first);
        });
        if (!known) families.emplace_back(bundled.first);
    }
    std::ranges::sort(families, {}, &font_family_key);
    return families;
}

float text_preview_scale(std::string_view family) {
    const LoadedFont* font = load_font(family);
    if (font == nullptr) return 1.0f;
    const float per_height = stbtt_ScaleForPixelHeight(&font->info, 1.0f);
    const float per_em = stbtt_ScaleForMappingEmToPixels(&font->info, 1.0f);
    if (per_em <= 0.0f) return 1.0f;
    return per_height / per_em;
}

std::vector<float> text_line_widths(std::string_view text, const TextOptions& options) {
    std::vector<float> widths;
    const LoadedFont* font = load_font(options.family);
    if (font == nullptr || options.size <= 0) return widths;

    const float scale = stbtt_ScaleForPixelHeight(&font->info, options.size);
    // The advances are rounded the way draw_text rounds them, so a rule can end
    // on the same pixel as the glyphs it decorates.
    int width = 0;
    int previous = 0;
    std::size_t index = 0;
    while (index < text.size()) {
        const std::uint32_t codepoint = next_codepoint(text, index);
        if (codepoint == '\n') {
            widths.push_back(static_cast<float>(width));
            width = 0;
            previous = 0;
            continue;
        }

        int advance = 0;
        int left_side_bearing = 0;
        stbtt_GetCodepointHMetrics(&font->info, static_cast<int>(codepoint), &advance,
                                   &left_side_bearing);
        if (previous != 0) {
            width += static_cast<int>(
                std::lround(stbtt_GetCodepointKernAdvance(&font->info, previous,
                                                          static_cast<int>(codepoint)) *
                            scale));
        }
        width += static_cast<int>(std::lround(advance * scale));
        previous = static_cast<int>(codepoint);
    }
    widths.push_back(static_cast<float>(width));
    return widths;
}

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
    // The top of the line the pen is on, and the marks that decorate it. Both
    // follow the pen down the lines.
    int line_top = pen_y - static_cast<int>(std::lround(ascent * scale));
    const int mark_thickness =
        std::max(1, static_cast<int>(std::lround(options.size * kMarkThickness)));
    const int underline_drop =
        std::max(1, static_cast<int>(std::lround(options.size * kUnderlineDrop)));
    const int strike_rise = static_cast<int>(std::lround(options.size * kStrikeRise));

    RectI touched;
    int previous = 0;
    std::size_t index = 0;
    while (index < text.size()) {
        const std::uint32_t codepoint = next_codepoint(text, index);
        if (codepoint == '\n') {
            pen_x = left;
            pen_y += line_height;
            line_top += line_height;
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

        const int pen_start = pen_x;
        const int advance_px = static_cast<int>(std::lround(advance * scale));
        if (options.opaque_background && advance_px > 0) {
            const RectI band{pen_start, line_top, advance_px, line_height};
            raster::draw_rect_fill(target, band, options.background);
            touched = touched.united(band);
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

        // The marks follow the glyphs, so a descender cannot cut the rule.
        if (advance_px > 0) {
            if (options.underline) {
                const RectI bar{pen_start, pen_y + underline_drop, advance_px, mark_thickness};
                raster::draw_rect_fill(target, bar, color);
                touched = touched.united(bar);
            }
            if (options.strikethrough) {
                const RectI bar{pen_start, pen_y - strike_rise, advance_px, mark_thickness};
                raster::draw_rect_fill(target, bar, color);
                touched = touched.united(bar);
            }
        }

        pen_x += advance_px;
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
