#include "app/app_controller.hpp"

#include <algorithm>

#include "config.hpp"
#include "history/commands/canvas_resize_command.hpp"
#include "history/commands/paint_stroke_command.hpp"
#include "history/commands/selection_edit_command.hpp"
#include "io/clipboard.hpp"
#include "selection/selection_ops.hpp"
#include "tools/stroke_recorder.hpp"
#include "utils/path_text.hpp"

namespace lsp {

namespace {

// What the clipboard holds when it holds no image, so a paste that fails can be
// diagnosed from the console.
std::string clipboard_contents_note() {
    const std::vector<std::string> types = Clipboard::available_types();
    std::string note = "; the clipboard offers ";
    if (types.empty()) {
        note += "nothing";
    } else {
        for (std::size_t index = 0; index < types.size(); ++index) {
            if (index != 0) note += ", ";
            note += types[index];
        }
        if (Clipboard::has_image()) note += ", but its image could not be read";
    }
    std::string text;
    if (!Clipboard::get_text(text) || text.empty()) return note + "; the clipboard holds no text";
    constexpr std::size_t kMaxShown = 200;
    const bool clipped = text.size() > kMaxShown;
    text.resize(std::min(text.size(), kMaxShown));
    for (char& letter : text) {
        if (letter == '\n' || letter == '\r' || letter == '\t') letter = ' ';
    }
    return note + "; clipboard text: \"" + text + "\"" + (clipped ? "..." : "");
}

}  // namespace

AppController::AppController()
    : m_document(kDefaultCanvasWidth, kDefaultCanvasHeight),
      m_history(m_document),
      m_context{m_document, m_history, m_brush, m_colors, m_selection_shape, m_shape_options,
                m_text_options, m_tool_ui},
      m_router(m_registry, m_context) {}

void AppController::commit_text(Point2f origin, std::string_view text, Rgba8 color) {
    if (text.empty()) return;
    const LayerTarget layer = m_context.layer_target();
    StrokeRecorder recorder(m_document);
    const RectI touched = draw_text(layer.target(), origin, text, m_text_options, color);
    if (touched.empty()) return;
    recorder.touch(touched);
    recorder.commit(m_history);
}

void AppController::resize_canvas(int width, int height, Rgba8 fill) {
    if (width == m_document.width() && height == m_document.height()) return;
    edit_geometry([&] { m_document.resize(width, height, fill); });
}

void AppController::crop_to_selection() {
    const RectI region = m_document.selection().bounds();
    if (region.empty()) return;
    edit_geometry([&] { m_document.crop_to(region); });
}

void AppController::rotate(CanvasDocument::Rotation rotation) {
    edit_geometry([&] { m_document.rotate(rotation); });
}

void AppController::flip_horizontal() {
    edit_geometry([&] { m_document.flip_horizontal(); });
}

void AppController::flip_vertical() {
    edit_geometry([&] { m_document.flip_vertical(); });
}

// Whole-image geometry always ends deselected: the mask belongs to the old
// geometry, so only undo has anything to put back.
void AppController::push_geometry_edit(std::vector<PixelBuffer> before, SelectionMask selection_before) {
    m_history.push(std::make_unique<CanvasResizeCommand>(std::move(before), std::move(selection_before),
                                                         std::nullopt));
}

void AppController::commit_selection_contents() { m_context.commit_selection(); }

bool AppController::discard_floating() {
    if (!m_document.floating()) return false;
    m_document.floating().reset();
    m_document.selection().clear();
    m_document.mark_dirty(m_document.bounds());
    return true;
}

// Makes a floating block the live selection: the contents hover over the
// canvas while the mask — and so the marching ants — describes where they sit.
void AppController::install_floating(FloatingContents contents) {
    const RectI position = contents.position;
    const std::vector<std::uint8_t> mask = contents.mask;
    m_document.floating() = std::move(contents);
    m_document.selection().clear();
    m_document.selection().replace_region(position, mask);
    m_document.mark_dirty(position);
}

void AppController::select_all() {
    commit_selection_contents();
    m_document.selection().fill_rect(m_document.bounds());
}

void AppController::invert_selection() {
    commit_selection_contents();
    m_document.selection().invert();
}

void AppController::clear_selection() {
    commit_selection_contents();
    m_document.selection().clear();
}

void AppController::make_selection_transparent() {
    commit_selection_contents();
    const SelectionMask& selection = m_document.selection();
    const RectI region = selection.active() ? selection.bounds() : m_document.bounds();
    if (region.empty()) return;

    const std::size_t layer_index = m_document.layers().active_index();
    PixelBuffer& pixels = m_document.active_layer().pixels;
    PixelBuffer before = pixels.copy_region(region);
    make_transparent(pixels, selection, region);
    m_document.mark_dirty(region);
    m_history.push(std::make_unique<PaintStrokeCommand>(layer_index, region, std::move(before), pixels.copy_region(region)));
}

void AppController::push_selection_edit(SelectionEdit edit) {
    if (edit.empty()) return;
    // A float-only edit never touches a pixel region, but taking the float
    // away (or moving it) repaints everything it covered.
    if (edit.float_before || edit.float_after)
        m_document.mark_dirty(m_document.bounds());
    else
        m_document.mark_dirty(edit.region);
    m_history.push(
        make_selection_edit(m_document.layers().active_index(), std::move(edit)));
}

void AppController::copy_selection() {
    if (m_document.floating()) {
        m_clipboard = capture_floating(*m_document.floating(), m_document.bounds());
    } else {
        m_clipboard = capture_clipboard(m_document.active_layer().pixels, m_document.selection());
    }
    // The same pixels also go to the system clipboard, so they can be pasted
    // into another application. The selection clipboard already holds them.
    (void)copy_to_clipboard();
}

void AppController::cut_selection() {
    copy_selection();
    delete_selection_contents();
}

void AppController::delete_selection_contents() {
    push_selection_edit(erase_contents(m_document.active_layer().pixels, m_document.selection(),
                                       m_document.floating(),
                                       m_colors.ink(PointerButton::Secondary)));
}

void AppController::paste_clipboard() {
    if (m_clipboard.empty()) return;
    commit_selection_contents();

    // The block hovers over the canvas until the paste is applied, so the
    // pixels it covers are left alone for now.
    SelectionEdit edit;
    edit.mask_before = m_document.selection();
    install_floating(float_block(m_clipboard.pixels, m_clipboard.mask, 0, 0));
    edit.mask_after = m_document.selection();
    edit.float_after = m_document.floating();
    push_selection_edit(std::move(edit));
}

void AppController::transform_selection_contents(ContentTransform transform) {
    push_selection_edit(transform_contents(m_document.active_layer().pixels, m_document.selection(),
                                           m_document.floating(), transform,
                                           m_colors.ink(PointerButton::Secondary)));
}

IoResult AppController::new_document() {
    m_document.restore({PixelBuffer(kDefaultCanvasWidth, kDefaultCanvasHeight, kWhite)});
    m_history.clear();
    m_current_path.clear();
    return IoResult::success();
}

IoResult AppController::open_document(const std::filesystem::path& path) {
    LoadResult loaded = load(path);
    if (!loaded.ok) return IoResult::failure(loaded.error);
    if (loaded.pixels.width() > kMaxCanvasDimension || loaded.pixels.height() > kMaxCanvasDimension)
        return IoResult::failure("Image is larger than the canvas limit: " + utf8_from_path(path));

    m_document.restore({std::move(loaded.pixels)});
    m_history.clear();
    m_current_path = path;
    return IoResult::success();
}

IoResult AppController::save_document(const std::filesystem::path& path, ImageFormat format) {
    (void)m_document.flush();
    std::string error;
    if (!save(m_document, path, format, &error)) return IoResult::failure(error);
    m_current_path = path;
    m_history.mark_clean();
    return IoResult::success();
}

IoResult AppController::save_document() {
    if (!has_current_path()) return IoResult::failure("No file name yet");
    const std::optional<ImageFormat> format = format_from_extension(m_current_path);
    if (!format) return IoResult::failure("Unknown file extension: " + utf8_from_path(m_current_path));
    return save_document(m_current_path, *format);
}

std::string AppController::display_name() const {
    return has_current_path() ? utf8_from_path(m_current_path.filename()) : "untitled";
}

std::filesystem::path AppController::default_directory() const {
    if (has_current_path()) {
        const std::filesystem::path parent = m_current_path.parent_path();
        if (!parent.empty()) return parent;
    }
    return std::filesystem::current_path();
}

RectI AppController::copy_source_rect() const {
    const SelectionMask& selection = m_document.selection();
    if (selection.active()) return selection.bounds().intersected(m_document.bounds());
    return m_document.bounds();
}

IoResult AppController::copy_to_clipboard() {
    (void)m_document.flush();
    const RectI region = copy_source_rect();
    if (region.empty()) return IoResult::failure("There is nothing to copy");
    if (!Clipboard::set_image(m_document.flattened().copy_region(region)))
        return IoResult::failure("The system clipboard is not available");
    return IoResult::success();
}

IoResult AppController::paste_from_clipboard() {
    std::string error;
    std::optional<PixelBuffer> image = Clipboard::get_image(&error);
    if (!image) {
        if (!error.empty()) return IoResult::failure(error);
        return IoResult::failure("The clipboard does not contain an image" + clipboard_contents_note());
    }
    return paste_image(std::move(*image));
}

IoResult AppController::paste_from_file(const std::filesystem::path& path) {
    LoadResult loaded = load(path);
    if (!loaded.ok) return IoResult::failure(loaded.error);
    return paste_image(std::move(loaded.pixels));
}

IoResult AppController::paste_image(PixelBuffer image) {
    if (image.width() <= 0 || image.height() <= 0) return IoResult::failure("There is nothing to paste");
    if (image.width() > kMaxCanvasDimension || image.height() > kMaxCanvasDimension)
        return IoResult::failure("The image is larger than the canvas limit");

    const int width = std::max(m_document.width(), image.width());
    const int height = std::max(m_document.height(), image.height());

    // The canvas grows only where the pasted image needs it. The block itself
    // only hovers on it: nothing is painted over until the paste is applied,
    // which is what lets it be moved and resized first.
    edit_geometry([&] {
        if (width != m_document.width() || height != m_document.height())
            m_document.resize(width, height, kWhite);
        install_floating(float_block(std::move(image), {}, 0, 0));
    });
    return IoResult::success();
}

}  // namespace lsp
