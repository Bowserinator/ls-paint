#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/raster.hpp"
#include "core/types.hpp"
#include "tools/tool.hpp"

// Text placement and the tool that opens a text box.
//
// The editing happens in a UI overlay, so this side only owns the state the
// overlay reads and the rasterizer that turns a committed string into pixels.
namespace lsp {

// How a committed block is laid out. The rasterizer synthesizes the bold and
// italic faces, so one bundled file covers each family.
struct TextOptions {
    std::string family = "DejaVu Sans";
    float size = 16.0f;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    // An opaque block fills the band behind each line with `background` before
    // the glyphs go in.
    bool opaque_background = false;
    Rgba8 background = kWhite;
};

// The text box the overlay is editing: where a press placed it, and the colour
// it will be committed in.
struct TextOverlayState {
    bool visible = false;
    Point2f origin;
    Rgba8 color{0, 0, 0, kChannelMax};
};

// Every family the text tool can draw with, sorted by name: the installed ones
// plus the bundled ones the platform does not report.
[[nodiscard]] std::vector<std::string> text_font_families();

// The factor the overlay's font size is multiplied by so that its text is as
// large as draw_text draws the same size: the rasterizer measures a size as the
// face's ascent plus descent, a text engine measures it as the em.
[[nodiscard]] float text_preview_scale(std::string_view family);

// The width in document pixels of every line of the text, in the order the
// lines are drawn and counting the one after a trailing newline.
[[nodiscard]] std::vector<float> text_line_widths(std::string_view text, const TextOptions& options);

// The distance in pixels from one line of text to the next.
[[nodiscard]] int text_line_height(const TextOptions& options);

// What a block of text will look like once committed: the pixels draw_text
// paints for it, on a transparent sheet. The text starts `margin` pixels in
// from the top left of the sheet, so glyphs that overhang their line box are
// not cut off. `width` and `height` are the size of the text's line boxes.
struct TextPreview {
    PixelBuffer pixels;
    int margin = 0;
    int width = 0;
    int height = 0;
};

// The sheet is empty for empty text, but the size of the line boxes is still
// reported.
[[nodiscard]] TextPreview render_text_preview(std::string_view text, const TextOptions& options,
                                              Rgba8 color);

// Where the text cursor sits, in pixels from the top left of the first line box,
// when it stands before byte `byte_offset` of the text.
struct TextCaret {
    int x = 0;
    int y = 0;
    int height = 0;
};

[[nodiscard]] TextCaret text_caret(std::string_view text, std::size_t byte_offset,
                                   const TextOptions& options);

// Draws the UTF-8 text with the top left of its first line box at `origin`,
// which is where the overlay shows it. Newlines start a new line below, and the
// target's clip mask still applies. Returns the touched rect, empty when
// nothing was drawn.
[[nodiscard]] RectI draw_text(raster::Target target, Point2f origin, std::string_view text,
                              const TextOptions& options, Rgba8 color);

class TextTool final : public ITool {
public:
    void on_pointer_down(ToolContext& context, Point2f position, PointerButton button) override;
    // The box is transient: leaving the tool drops it, committed or not.
    void on_deactivate(ToolContext& context) override;
};

}  // namespace lsp
