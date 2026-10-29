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
#include "utils/math.hpp"
#include "utils/path_text.hpp"

#ifndef LSP_FONT_DIR
#define LSP_FONT_DIR ""
#endif

namespace lsp {
namespace {

constexpr float kItalicSlant = 0.21f;  // tan(12 degrees): the shear of a faux italic
// A bold glyph is widened by one pixel for every this many pixels of size.
constexpr float kBoldPixelsPerSize = 16.0f;
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
    std::ifstream stream{path_from_utf8(path), std::ios::binary};
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
    if (const auto found = cache.find(key); found != cache.end())
        return found->second.ready ? &found->second : nullptr;

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

// Families tried, in this order, for a character the requested family lacks.
// They cover the scripts the bundled faces do not; anything they miss is looked
// up among every installed face.
constexpr std::string_view kFallbackFamilies[]{
    "Microsoft YaHei", "Microsoft JhengHei", "Yu Gothic",       "Malgun Gothic",
    "PingFang SC",     "Hiragino Sans",      "Noto Sans CJK SC", "Noto Sans CJK JP",
    "Noto Sans CJK TC", "Noto Sans CJK KR",  "Noto Sans SC",     "Source Han Sans SC",
    "WenQuanYi Micro Hei", "WenQuanYi Zen Hei", "Arial Unicode MS", "Segoe UI Symbol",
};

// The face in `path`, loaded once. Nothing when the file is unusable.
const LoadedFont* font_at_path(const std::string& path) {
    static std::map<std::string, LoadedFont> cache;
    auto found = cache.find(path);
    if (found == cache.end()) {
        LoadedFont font;
        (void)read_font(font, path);
        // A file that fails is only remembered as a failure, not as megabytes
        // of bytes no glyph will ever come from.
        if (!font.ready) font.bytes.clear();
        found = cache.emplace(path, std::move(font)).first;
    }
    return found->second.ready ? &found->second : nullptr;
}

[[nodiscard]] bool has_glyph(const LoadedFont& font, std::uint32_t codepoint) {
    return stbtt_FindGlyphIndex(&font.info, static_cast<int>(codepoint)) != 0;
}

// An installed face that draws `codepoint`, nothing when there is none.
const LoadedFont* fallback_font(std::uint32_t codepoint) {
    static std::map<std::uint32_t, const LoadedFont*> cache;
    if (const auto found = cache.find(codepoint); found != cache.end()) return found->second;

    // The preferred families are read one at a time as far as the search needs,
    // because each file can be tens of megabytes.
    const LoadedFont* result = nullptr;
    for (const std::string_view family : kFallbackFamilies) {
        const std::string path = installed_fonts().face_path(family);
        if (path.empty()) continue;
        const LoadedFont* font = font_at_path(path);
        if (font != nullptr && has_glyph(*font, codepoint)) {
            result = font;
            break;
        }
    }
    if (result == nullptr) {
        const std::string path = installed_fonts().face_path_covering(codepoint);
        const LoadedFont* font = path.empty() ? nullptr : font_at_path(path);
        if (font != nullptr && has_glyph(*font, codepoint)) result = font;
    }
    if (result == nullptr) {
        // The platform's match can name a face this rasterizer cannot read (a
        // CFF2 collection, say), so the installed files are then tried one by
        // one until one both loads and covers the character.
        for (const FontFace& face : installed_fonts().faces()) {
            if (face.path.empty()) continue;
            const LoadedFont* font = font_at_path(face.path);
            if (font != nullptr && has_glyph(*font, codepoint)) {
                result = font;
                break;
            }
        }
    }
    cache[codepoint] = result;
    return result;
}

// The face a character is drawn with and the scale it is drawn at.
struct GlyphFace {
    const LoadedFont* font = nullptr;
    float scale = 0;
};

// The requested face when it has the character, otherwise a fallback sized to
// the same em so mixed text keeps an even height.
GlyphFace face_for(const LoadedFont& primary, float primary_scale, std::uint32_t codepoint) {
    if (codepoint < 0x20 || has_glyph(primary, codepoint)) return {&primary, primary_scale};
    const LoadedFont* fallback = fallback_font(codepoint);
    if (fallback == nullptr) return {&primary, primary_scale};
    const float em_pixels = primary_scale / stbtt_ScaleForMappingEmToPixels(&primary.info, 1.0f);
    return {fallback, em_pixels * stbtt_ScaleForMappingEmToPixels(&fallback->info, 1.0f)};
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

// How far the pen moves before `codepoint` is drawn when `previous` came
// before it (0 for none), and how far it moves after, in whole pixels.
struct PenMove {
    int kern = 0;
    int advance = 0;
};

PenMove pen_move(const stbtt_fontinfo& info, int previous, std::uint32_t codepoint, float scale) {
    int advance = 0;
    int left_side_bearing = 0;
    stbtt_GetCodepointHMetrics(&info, static_cast<int>(codepoint), &advance, &left_side_bearing);
    PenMove move;
    if (previous != 0) {
        move.kern = round_to_int(
            static_cast<float>(stbtt_GetCodepointKernAdvance(&info, previous, static_cast<int>(codepoint))) *
            scale);
    }
    move.advance = round_to_int(static_cast<float>(advance) * scale);
    return move;
}

// The pen move for `codepoint` drawn with `face`. Kerning only applies between
// two characters of the same font.
PenMove pen_move_for(const GlyphFace& face, const LoadedFont* previous_font, int previous,
                     std::uint32_t codepoint) {
    return pen_move(face.font->info, previous_font == face.font ? previous : 0, codepoint, face.scale);
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
// shear shifts a row further right the higher it sits above the baseline, and
// the dilation widens every row to the right, so both grow the bitmap.
GlyphBitmap shape_glyph(const unsigned char* raw, int width, int height, int x_offset, int y_offset,
                        const TextOptions& options) {
    GlyphBitmap glyph;
    const auto shear_at = [&options](int y) {
        return options.italic ? std::max(0, round_to_int(-y * kItalicSlant)) : 0;
    };
    const int shear = shear_at(y_offset);
    const int passes =
        options.bold ? std::max(1, round_to_int(options.size / kBoldPixelsPerSize)) : 0;
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
        const int shift = shear_at(y_offset + row);
        for (int column = 0; column < width; ++column) {
            glyph.coverage[grid_index(column + shift, row, glyph.width)] =
                raw[grid_index(column, row, width)];
        }
    }
    if (passes > 0) {
        const std::vector<std::uint8_t> sheared = glyph.coverage;
        for (int row = 0; row < height; ++row) {
            for (int column = 0; column < width + shear; ++column) {
                const std::uint8_t value = sheared[grid_index(column, row, glyph.width)];
                for (int step = 1; step <= passes; ++step) {
                    auto& target = glyph.coverage[grid_index(column + step, row, glyph.width)];
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
    const LoadedFont* previous_font = nullptr;
    std::size_t index = 0;
    while (index < text.size()) {
        const std::uint32_t codepoint = next_codepoint(text, index);
        if (codepoint == '\n') {
            widths.push_back(static_cast<float>(width));
            width = 0;
            previous = 0;
            continue;
        }

        const GlyphFace face = face_for(*font, scale, codepoint);
        const PenMove move = pen_move_for(face, previous_font, previous, codepoint);
        width += move.kern + move.advance;
        previous = static_cast<int>(codepoint);
        previous_font = face.font;
    }
    widths.push_back(static_cast<float>(width));
    return widths;
}

int text_line_height(const TextOptions& options) {
    const LoadedFont* font = load_font(options.family);
    if (font == nullptr || options.size <= 0) return 0;
    const float scale = stbtt_ScaleForPixelHeight(&font->info, options.size);
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&font->info, &ascent, &descent, &line_gap);
    return std::max(1, round_to_int(static_cast<float>(ascent - descent + line_gap) * scale));
}

TextPreview render_text_preview(std::string_view text, const TextOptions& options, Rgba8 color) {
    TextPreview preview;
    const int line_height = text_line_height(options);
    if (line_height <= 0) return preview;

    const std::vector<float> widths = text_line_widths(text, options);
    const float widest = widths.empty() ? 0.0f : *std::max_element(widths.begin(), widths.end());
    preview.width = ceil_to_int(widest);
    preview.height = line_height * static_cast<int>(std::max<std::size_t>(widths.size(), 1));
    if (text.empty()) return preview;

    // A glyph can lean or thicken past its line box by a fraction of the size.
    const int margin = static_cast<int>(options.size / 2.0f) + 4;
    const long long sheet_width = static_cast<long long>(preview.width) + 2 * margin;
    const long long sheet_height = static_cast<long long>(preview.height) + 2 * margin;
    constexpr long long kMaxSheetPixels = 16'000'000;
    if (sheet_width * sheet_height > kMaxSheetPixels) return preview;

    preview.margin = margin;
    preview.pixels = PixelBuffer(static_cast<int>(sheet_width), static_cast<int>(sheet_height), kTransparent);
    (void)draw_text(raster::Target{preview.pixels}, {static_cast<float>(margin), static_cast<float>(margin)},
                    text, options, color);
    return preview;
}

TextCaret text_caret(std::string_view text, std::size_t byte_offset, const TextOptions& options) {
    TextCaret caret;
    caret.height = text_line_height(options);
    const std::vector<float> widths = text_line_widths(text.substr(0, std::min(byte_offset, text.size())), options);
    if (widths.empty()) return caret;
    caret.x = round_to_int(widths.back());
    caret.y = caret.height * static_cast<int>(widths.size() - 1);
    return caret;
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
    const int line_height = text_line_height(options);

    // The press marks the top left of the first line box.
    const int left = round_to_int(origin.x);
    int pen_x = left;
    int pen_y = round_to_int(origin.y + static_cast<float>(ascent) * scale);
    // The top of the line the pen is on, and the marks that decorate it. Both
    // follow the pen down the lines.
    int line_top = pen_y - round_to_int(static_cast<float>(ascent) * scale);
    const int mark_thickness = std::max(1, round_to_int(options.size * kMarkThickness));
    const int underline_drop = std::max(1, round_to_int(options.size * kUnderlineDrop));
    const int strike_rise = round_to_int(options.size * kStrikeRise);

    RectI touched;
    const auto draw_bar = [&](const RectI& bar, Rgba8 bar_color) {
        raster::draw_rect_fill(target, bar, bar_color);
        touched = touched.united(bar);
    };
    std::size_t index = 0;
    // One line at a time, in three passes: the background bands, then the
    // glyphs over them, then the rules. A band drawn between two glyphs would
    // paint over the tail a slanted glyph leaves over its neighbour.
    struct Placed {
        std::uint32_t codepoint;
        int pen_x;
        int advance_px;
        GlyphFace face;
    };
    std::vector<Placed> line;
    while (index < text.size()) {
        line.clear();
        int previous = 0;
        const LoadedFont* previous_font = nullptr;
        bool wrapped = false;
        while (index < text.size()) {
            const std::uint32_t codepoint = next_codepoint(text, index);
            if (codepoint == '\n') {
                wrapped = true;
                break;
            }

            const GlyphFace face = face_for(*font, scale, codepoint);
            const PenMove move = pen_move_for(face, previous_font, previous, codepoint);
            pen_x += move.kern;

            const int pen_start = pen_x;
            const int advance_px = move.advance;
            if (options.opaque_background && advance_px > 0) {
                draw_bar({pen_start, line_top, advance_px, line_height}, options.background);
            }
            line.push_back({codepoint, pen_start, advance_px, face});
            pen_x += advance_px;
            previous = static_cast<int>(codepoint);
            previous_font = face.font;
        }

        for (const Placed& placed : line) {
            int width = 0;
            int height = 0;
            int x_offset = 0;
            int y_offset = 0;
            unsigned char* raw = stbtt_GetCodepointBitmap(&placed.face.font->info, placed.face.scale,
                                                          placed.face.scale,
                                                          static_cast<int>(placed.codepoint), &width,
                                                          &height, &x_offset, &y_offset);
            if (raw != nullptr) {
                const GlyphBitmap glyph =
                    shape_glyph(raw, width, height, x_offset, y_offset, options);
                if (glyph.width > 0 && glyph.height > 0) {
                    const RectI box{placed.pen_x + glyph.x, pen_y + glyph.y, glyph.width,
                                    glyph.height};
                    raster::draw_glyph(target, box.x, box.y, glyph.width, glyph.height, glyph.width,
                                       glyph.coverage.data(), color);
                    touched = touched.united(box);
                }
                stbtt_FreeBitmap(raw, nullptr);
            }
        }

        // The marks follow the glyphs, so a descender cannot cut the rule.
        for (const Placed& placed : line) {
            if (placed.advance_px <= 0) continue;
            if (options.underline)
                draw_bar({placed.pen_x, pen_y + underline_drop, placed.advance_px, mark_thickness}, color);
            if (options.strikethrough)
                draw_bar({placed.pen_x, pen_y - strike_rise, placed.advance_px, mark_thickness}, color);
        }

        if (!wrapped) break;
        pen_x = left;
        pen_y += line_height;
        line_top += line_height;
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
