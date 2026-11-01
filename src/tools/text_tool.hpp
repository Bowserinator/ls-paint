#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/raster.hpp"
#include "core/types.hpp"
#include "tools/selection_handles.hpp"
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

// How one character of a block is drawn.
struct TextStyle {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    Rgba8 color{0, 0, 0, kChannelMax};

    friend bool operator==(const TextStyle&, const TextStyle&) = default;
};

// The styles of a text, indexed by byte offset, after an edit turned `before`
// into `after`. Bytes the edit left alone keep their style and the bytes it
// added take `inserted`. `caret` is where the cursor stands in `after`, which
// settles where a repeated character was added.
[[nodiscard]] std::vector<TextStyle> restyled_after_edit(std::string_view before, std::string_view after,
                                                         std::span<const TextStyle> styles,
                                                         const TextStyle& inserted, std::size_t caret);

// The text box the overlay is editing: where a press placed it, and the colour
// it will be committed in.
struct TextOverlayState {
    bool visible = false;
    Point2f origin;
    Rgba8 color{0, 0, 0, kChannelMax};
    std::vector<TextStyle> styles;
    // The size of the box in document pixels. It follows the text until a drag
    // gives it a size of its own, and typing takes that away again.
    float width = 0.0f;
    float height = 0.0f;
    bool sized = false;
};

// A span of bytes of a text, empty when `begin` equals `end`.
struct ByteRange {
    std::size_t begin = 0;
    std::size_t end = 0;
};

// The box as a rectangle of document pixels.
[[nodiscard]] RectI text_box_bounds(const TextOverlayState& text);

// Whether a press at `position` takes hold of the box: on a grip, or on the
// edge a little way in or out of it. `zoom` is screen pixels per document pixel.
[[nodiscard]] bool text_box_grabs(const TextOverlayState& text, Point2f position, float zoom);

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
// reported. Characters without an entry in `styles` take the look `options` and
// `color` give. Characters inside `selection` are drawn as a selection.
[[nodiscard]] TextPreview render_text_preview(std::string_view text, const TextOptions& options,
                                              Rgba8 color, std::span<const TextStyle> styles = {},
                                              ByteRange selection = {});

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
// target's clip mask still applies. Characters without an entry in `styles` take
// the look `options` and `color` give. Characters inside `selection` are drawn
// light on a blue band, as an editor shows selected text. Returns the touched
// rect, empty when nothing was drawn.
[[nodiscard]] RectI draw_text(raster::Target target, Point2f origin, std::string_view text,
                              const TextOptions& options, Rgba8 color,
                              std::span<const TextStyle> styles = {}, ByteRange selection = {});

class TextTool final : public ITool {
public:
    void on_pointer_down(ToolContext& context, Point2f position, PointerButton button) override;
    void on_pointer_move(ToolContext& context, Point2f position) override;
    void on_pointer_up(ToolContext& context, Point2f position) override;
    bool on_cancel(ToolContext& context) override;
    // The box is transient: leaving the tool drops it, committed or not.
    void on_deactivate(ToolContext& context) override;

private:
    enum class Drag { None, Move, Resize };

    Drag m_drag = Drag::None;
    Point2f m_start;
    Point2f m_origin_before;
    RectI m_bounds_before;
    SelectionHandle m_handle = SelectionHandle::Nw;
};

}  // namespace lsp
