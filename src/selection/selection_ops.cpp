#include "selection/selection_ops.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace lsp {

namespace {

// A total turn below this is rounding noise, not a turn.
constexpr float kNoTurnDegrees = 1e-3f;

PixelBuffer apply_transform(const PixelBuffer& source, ContentTransform transform) {
    switch (transform) {
        case ContentTransform::RotateCw90: return source.rotated_90_cw();
        case ContentTransform::RotateCcw90:
            return source.rotated_90_cw().rotated_90_cw().rotated_90_cw();
        case ContentTransform::Rotate180: return source.flipped_vertical().flipped_horizontal();
        case ContentTransform::FlipHorizontal: return source.flipped_horizontal();
        case ContentTransform::FlipVertical: return source.flipped_vertical();
    }
    return source;
}

// 0/1 mask bytes packed one per pixel so the mask can travel through the same
// PixelBuffer transform as the pixels it masks.
PixelBuffer bytes_to_pixels(const std::vector<std::uint8_t>& bytes, int width, int height) {
    PixelBuffer pixels(width, height, {0, 0, 0, 255});
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const std::uint8_t value = bytes[static_cast<std::size_t>(y) * width + x] ? 255 : 0;
            pixels.at(x, y) = {value, value, value, 255};
        }
    return pixels;
}

std::vector<std::uint8_t> pixels_to_bytes(const PixelBuffer& pixels) {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(pixels.width()) * pixels.height(), 0);
    for (int y = 0; y < pixels.height(); ++y)
        for (int x = 0; x < pixels.width(); ++x)
            bytes[static_cast<std::size_t>(y) * pixels.width() + x] = pixels.at(x, y).r != 0 ? 1 : 0;
    return bytes;
}

// Writes only the masked pixels of `source`, so unselected destinations survive.
void paste_masked(PixelBuffer& target, const PixelBuffer& source, int x, int y,
                  const std::vector<std::uint8_t>& mask) {
    for (int j = 0; j < source.height(); ++j) {
        const int target_y = y + j;
        if (target_y < 0 || target_y >= target.height()) continue;
        for (int i = 0; i < source.width(); ++i) {
            if (mask[static_cast<std::size_t>(j) * source.width() + i] == 0) continue;
            const int target_x = x + i;
            if (target_x < 0 || target_x >= target.width()) continue;
            target.at(target_x, target_y) = source.at(i, j);
        }
    }
}

// The background color is what shows through wherever a selection used to be,
// so vacated pixels are painted with it rather than left transparent.
void fill_selected(PixelBuffer& pixels, const SelectionMask& mask, RectI region, Rgba8 fill) {
    region = region.intersected(pixels.bounds());
    for (int y = region.y; y < region.bottom(); ++y)
        for (int x = region.x; x < region.right(); ++x)
            if (mask.contains(x, y)) pixels.at(x, y) = fill;
}

// Same fill, driven by region-local 0/1 bytes instead of a document mask — the
// shape a floating selection was lifted with travels with the float.
void fill_from_bytes(PixelBuffer& pixels, RectI rect, const std::vector<std::uint8_t>& bytes, RectI region,
                     Rgba8 fill) {
    if (rect.empty() || bytes.empty()) return;
    region = region.intersected(pixels.bounds()).intersected(rect);
    for (int y = region.y; y < region.bottom(); ++y)
        for (int x = region.x; x < region.right(); ++x)
            if (bytes[static_cast<std::size_t>(y - rect.y) * rect.width + (x - rect.x)]) pixels.at(x, y) = fill;
}

// Draws a region-local pixel block into `target` where its mask byte is set.
void stamp_from_bytes(PixelBuffer& target, RectI rect, const PixelBuffer& content,
                      const std::vector<std::uint8_t>& bytes, RectI region) {
    if (rect.empty() || content.width() <= 0 || bytes.empty()) return;
    region = region.intersected(target.bounds()).intersected(rect);
    for (int y = region.y; y < region.bottom(); ++y)
        for (int x = region.x; x < region.right(); ++x)
            if (bytes[static_cast<std::size_t>(y - rect.y) * rect.width + (x - rect.x)])
                target.at(x, y) = content.at(x - rect.x, y - rect.y);
}

PixelBuffer resample(const PixelBuffer& source, int width, int height) {
    if (width <= 0 || height <= 0 || source.width() <= 0 || source.height() <= 0) return {};
    PixelBuffer result(width, height);
    for (int y = 0; y < height; ++y) {
        const int source_y = std::min(source.height() - 1, y * source.height() / height);
        for (int x = 0; x < width; ++x) {
            const int source_x = std::min(source.width() - 1, x * source.width() / width);
            result.at(x, y) = source.at(source_x, source_y);
        }
    }
    return result;
}

std::vector<std::uint8_t> resample(const std::vector<std::uint8_t>& source, int source_width, int source_height,
                                   int width, int height) {
    if (width <= 0 || height <= 0 || source_width <= 0 || source_height <= 0 || source.empty()) return {};
    std::vector<std::uint8_t> result(static_cast<std::size_t>(width) * height, 0);
    for (int y = 0; y < height; ++y) {
        const int source_y = std::min(source_height - 1, y * source_height / height);
        for (int x = 0; x < width; ++x) {
            const int source_x = std::min(source_width - 1, x * source_width / width);
            result[static_cast<std::size_t>(y) * width + x] =
                source[static_cast<std::size_t>(source_y) * source_width + source_x];
        }
    }
    return result;
}

// A block turned about its centre: the pixels and the mask that says which of
// them belong to it.
struct TurnedBlock {
    PixelBuffer pixels;
    std::vector<std::uint8_t> mask;
};

// Turns a block by `radians`. The box grows so its corners fit, and a pixel is
// sampled from the single pixel its centre lands on, the way a rescale samples,
// so a turn never softens an edge. A destination that came from outside the
// block, or from a pixel the mask leaves out, stays unset.
TurnedBlock turned_block(const PixelBuffer& source, const std::vector<std::uint8_t>& source_mask,
                         float radians) {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    // A quarter turn has to come out exactly as the right-angle transform does,
    // so a trig value that is merely a rounding crumb away from zero counts as
    // zero. The sliver below the ceiling keeps a size that should be a whole
    // number one.
    const float straight = std::abs(cosine) < 1e-4f ? 0.0f : cosine;
    const float square = std::abs(sine) < 1e-4f ? 0.0f : sine;
    const int width = static_cast<int>(
        std::ceil(std::abs(straight) * source.width() + std::abs(square) * source.height() - 1e-3f));
    const int height = static_cast<int>(
        std::ceil(std::abs(square) * source.width() + std::abs(straight) * source.height() - 1e-3f));

    TurnedBlock result{PixelBuffer(width, height), std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height, 0)};
    if (source.width() <= 0 || source.height() <= 0) return result;

    const float source_centre_x = source.width() / 2.0f;
    const float source_centre_y = source.height() / 2.0f;
    const float centre_x = width / 2.0f;
    const float centre_y = height / 2.0f;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // Back from a turned pixel to the pixel its centre came from.
            const float dx = static_cast<float>(x) + 0.5f - centre_x;
            const float dy = static_cast<float>(y) + 0.5f - centre_y;
            const int source_x = static_cast<int>(std::floor(dx * straight + dy * square + source_centre_x));
            const int source_y = static_cast<int>(std::floor(-dx * square + dy * straight + source_centre_y));
            if (!source.contains(source_x, source_y)) continue;
            const std::size_t index = static_cast<std::size_t>(source_y) * source.width() + source_x;
            if (index >= source_mask.size() || source_mask[index] == 0) continue;
            result.pixels.at(x, y) = source.at(source_x, source_y);
            result.mask[static_cast<std::size_t>(y) * width + x] = 1;
        }
    }
    return result;
}

// Rebuilds the document mask from a floating selection's local mask, which is
// what keeps the two in step after a move, rotate or resize.
void sync_mask(SelectionMask& mask, const FloatingContents& contents) {
    mask.clear();
    mask.replace_region(contents.position, contents.mask);
}

// Brings `source` up to the turned state, for an operation that samples it
// directly. Turns after this one start a fresh run from the new box.
void bake_turn(FloatingContents& contents) {
    if (contents.rotation == 0.0f) return;
    contents.source = contents.content;
    contents.source_mask = contents.mask;
    contents.rotation = 0.0f;
}

}  // namespace

float wrap_degrees(float degrees) {
    float wrapped = std::fmod(degrees + 180.0f, 360.0f);
    if (wrapped < 0.0f) wrapped += 360.0f;
    return wrapped - 180.0f;
}

void make_transparent(PixelBuffer& pixels, const SelectionMask& mask, RectI region) {
    fill_selected(pixels, mask, region, {0, 0, 0, 0});
}

ClipboardContent capture_clipboard(const PixelBuffer& pixels, const SelectionMask& mask) {
    if (!mask.active() || mask.bounds().empty()) return {};
    const RectI region = mask.bounds().intersected(pixels.bounds());
    if (region.empty()) return {};

    ClipboardContent clip;
    clip.width = region.width;
    clip.height = region.height;
    clip.mask = mask.extract_region(region);
    clip.pixels = pixels.copy_region(region);
    return clip;
}

ClipboardContent capture_floating(const FloatingContents& contents, RectI canvas) {
    if (contents.empty()) return {};
    const RectI visible = contents.position.intersected(canvas);
    if (visible.empty()) return {};

    const RectI local{visible.x - contents.position.x, visible.y - contents.position.y, visible.width,
                      visible.height};
    ClipboardContent clip;
    clip.width = visible.width;
    clip.height = visible.height;
    clip.pixels = contents.content.copy_region(local);
    clip.mask.resize(static_cast<std::size_t>(clip.width) * clip.height);
    for (int y = 0; y < clip.height; ++y)
        for (int x = 0; x < clip.width; ++x)
            clip.mask[static_cast<std::size_t>(y) * clip.width + x] =
                contents.mask[static_cast<std::size_t>(local.y + y) * contents.position.width + (local.x + x)];
    return clip;
}

SelectionEdit erase_contents(PixelBuffer& pixels, SelectionMask& mask,
                             std::optional<FloatingContents>& floating, Rgba8 fill) {
    SelectionEdit edit;
    edit.mask_before = mask;
    edit.mask_after = mask;

    if (floating) {
        // The contents are not in the layer: discarding them only has to fill
        // the origin they were lifted from, so everything the float hovered
        // over in the meantime survives untouched.
        edit.float_before = floating;
        edit.float_after = std::nullopt;
        edit.region = floating->origin.intersected(pixels.bounds());
        if (!edit.region.empty()) {
            edit.before = pixels.copy_region(edit.region);
            fill_from_bytes(pixels, floating->origin, floating->origin_mask, edit.region, fill);
            edit.after = pixels.copy_region(edit.region);
        }
        floating.reset();
        return edit;
    }

    const RectI region = mask.bounds().intersected(pixels.bounds());
    if (region.empty()) return {};
    edit.region = region;
    edit.before = pixels.copy_region(region);
    fill_selected(pixels, mask, region, fill);
    edit.after = pixels.copy_region(region);
    return edit;
}

SelectionEdit paste_clipboard(PixelBuffer& pixels, SelectionMask& mask, const ClipboardContent& clip,
                              int x, int y) {
    if (clip.empty()) return {};
    const RectI target{x, y, clip.width, clip.height};
    const RectI region = target.intersected(pixels.bounds());
    if (region.empty()) return {};

    SelectionEdit edit;
    edit.region = region;
    edit.before = pixels.copy_region(region);
    edit.mask_before = mask;

    paste_masked(pixels, clip.pixels, x, y, clip.mask);

    mask.clear();
    mask.replace_region(target, clip.mask);

    edit.mask_after = mask;
    edit.after = pixels.copy_region(region);
    return edit;
}

FloatingContents float_block(PixelBuffer content, std::vector<std::uint8_t> mask, int x, int y) {
    FloatingContents contents;
    if (content.width() <= 0 || content.height() <= 0) return contents;

    const std::size_t count = static_cast<std::size_t>(content.width()) * content.height();
    if (mask.size() != count) mask.assign(count, 1);

    contents.position = RectI{x, y, content.width(), content.height()};
    contents.source = content;
    contents.source_mask = mask;
    contents.content = std::move(content);
    contents.mask = std::move(mask);
    return contents;
}

void lift_contents(const PixelBuffer& pixels, const SelectionMask& mask,
                   std::optional<FloatingContents>& floating, Rgba8 fill) {
    if (floating) return;
    const RectI bounds = mask.bounds().intersected(pixels.bounds());
    if (!mask.active() || bounds.empty()) return;

    FloatingContents contents;
    contents.position = bounds;
    contents.mask = mask.extract_region(bounds);
    contents.source_mask = contents.mask;
    contents.content = pixels.copy_region(bounds);
    contents.source = contents.content;
    contents.origin = bounds;
    contents.origin_mask = contents.mask;
    contents.fill = fill;
    floating = std::move(contents);
}

void translate_contents(SelectionMask& mask, FloatingContents& contents, int dx, int dy) {
    if (contents.empty() || (dx == 0 && dy == 0)) return;
    contents.position.x += dx;
    contents.position.y += dy;
    // The box a run of turns revolves about travels with the contents.
    contents.turn_bounds.x += dx;
    contents.turn_bounds.y += dy;
    sync_mask(mask, contents);
}

SelectionEdit transform_contents(const PixelBuffer& pixels, SelectionMask& mask,
                                 std::optional<FloatingContents>& floating, ContentTransform transform,
                                 Rgba8 fill) {
    if (!mask.active() || mask.bounds().empty()) return {};

    SelectionEdit edit;
    edit.mask_before = mask;
    edit.float_before = floating;  // before the lift: undo drops the float

    lift_contents(pixels, mask, floating, fill);
    if (!floating) return {};

    FloatingContents& contents = *floating;
    const RectI old_position = contents.position;
    bake_turn(contents);

    // The transform runs on the full-resolution source, and the visible copy
    // is resampled from the result. Rotating a squashed selection therefore
    // keeps its size while staying as sharp as the originals, so a later
    // scale-up is never based on a copy an earlier rescale had thinned.
    const int source_width = contents.source.width();
    const int source_height = contents.source.height();
    const PixelBuffer transformed_source = apply_transform(contents.source, transform);
    const std::vector<std::uint8_t> transformed_source_mask =
        pixels_to_bytes(apply_transform(bytes_to_pixels(contents.source_mask, source_width, source_height),
                                        transform));
    const PixelBuffer transformed_content = apply_transform(contents.content, transform);

    contents.source = transformed_source;
    contents.source_mask = transformed_source_mask;
    contents.position = {old_position.x + (old_position.width - transformed_content.width()) / 2,
                         old_position.y + (old_position.height - transformed_content.height()) / 2,
                         transformed_content.width(), transformed_content.height()};
    contents.content = resample(contents.source, contents.position.width, contents.position.height);
    contents.mask = resample(contents.source_mask, contents.source.width(), contents.source.height(),
                             contents.position.width, contents.position.height);
    sync_mask(mask, contents);

    edit.mask_after = mask;
    edit.float_after = floating;
    return edit;
}

SelectionEdit rotate_contents(const PixelBuffer& pixels, SelectionMask& mask,
                              std::optional<FloatingContents>& floating, float degrees, Rgba8 fill) {
    if (!mask.active() || mask.bounds().empty()) return {};

    SelectionEdit edit;
    edit.mask_before = mask;
    edit.float_before = floating;
    lift_contents(pixels, mask, floating, fill);
    if (!floating) return {};

    FloatingContents& contents = *floating;
    if (contents.rotation == 0.0f) contents.turn_bounds = contents.position;
    contents.rotation = wrap_degrees(contents.rotation + degrees);
    if (std::abs(contents.rotation) < kNoTurnDegrees) contents.rotation = 0.0f;

    // Every turn lays the source out at the run's box and turns it by the total
    // angle, so a second turn resamples the lifted originals rather than the
    // copy the first one made: a run of turns cannot pile up losses, and the
    // box grows around the centre the run started with.
    const int base_width = contents.turn_bounds.width;
    const int base_height = contents.turn_bounds.height;
    const bool laid_out =
        contents.source.width() == base_width && contents.source.height() == base_height;
    const PixelBuffer base_pixels =
        laid_out ? contents.source : resample(contents.source, base_width, base_height);
    const std::vector<std::uint8_t> base_mask =
        laid_out ? contents.source_mask
                 : resample(contents.source_mask, contents.source.width(), contents.source.height(),
                            base_width, base_height);

    const float radians = contents.rotation * std::numbers::pi_v<float> / 180.0f;
    TurnedBlock turned = turned_block(base_pixels, base_mask, radians);
    const float centre_x = contents.turn_bounds.x + base_width / 2.0f;
    const float centre_y = contents.turn_bounds.y + base_height / 2.0f;
    contents.position = {static_cast<int>(std::lround(centre_x - turned.pixels.width() / 2.0f)),
                         static_cast<int>(std::lround(centre_y - turned.pixels.height() / 2.0f)),
                         turned.pixels.width(), turned.pixels.height()};
    contents.content = std::move(turned.pixels);
    contents.mask = std::move(turned.mask);
    // A run that has turned all the way round is over: the next turn starts a
    // fresh one.
    if (contents.rotation == 0.0f) contents.turn_bounds = {};
    sync_mask(mask, contents);

    edit.mask_after = mask;
    edit.float_after = floating;
    return edit;
}

SelectionEdit resize_selection(SelectionMask& mask, std::optional<FloatingContents>& floating,
                               RectI new_bounds) {
    if (new_bounds.empty() || !mask.active() || mask.bounds().empty()) return {};

    SelectionEdit edit;
    edit.mask_before = mask;
    edit.float_before = floating;

    if (floating) {
        FloatingContents& contents = *floating;
        bake_turn(contents);
        // Every rescale samples the lifted originals, never the output of an
        // earlier one: squashing and then stretching back restores the pixels
        // it started with instead of magnifying the thinned copy.
        const PixelBuffer& sample = contents.source.width() > 0 ? contents.source : contents.content;
        const std::vector<std::uint8_t>& sample_mask =
            contents.source_mask.empty() ? contents.mask : contents.source_mask;
        contents.content = resample(sample, new_bounds.width, new_bounds.height);
        contents.mask = resample(sample_mask, sample.width(), sample.height(), new_bounds.width,
                                 new_bounds.height);
        contents.position = new_bounds;
        sync_mask(mask, contents);
    } else {
        const RectI old_bounds = mask.bounds();
        const std::vector<std::uint8_t> bytes =
            resample(mask.extract_region(old_bounds), old_bounds.width, old_bounds.height, new_bounds.width,
                     new_bounds.height);
        mask.clear();
        mask.replace_region(new_bounds, bytes);
    }

    edit.mask_after = mask;
    edit.float_after = floating;
    return edit;
}

SelectionEdit commit_contents(PixelBuffer& pixels, SelectionMask& mask,
                              std::optional<FloatingContents>& floating) {
    if (!floating || floating->empty()) return {};

    SelectionEdit edit;
    edit.mask_before = mask;
    edit.mask_after = mask;
    edit.float_before = floating;
    edit.float_after = std::nullopt;
    edit.region = floating->origin.united(floating->position).intersected(pixels.bounds());

    if (!edit.region.empty()) {
        edit.before = pixels.copy_region(edit.region);
        fill_from_bytes(pixels, floating->origin, floating->origin_mask, edit.region, floating->fill);
        stamp_from_bytes(pixels, floating->position, floating->content, floating->mask, edit.region);
        edit.after = pixels.copy_region(edit.region);
    }
    floating.reset();
    return edit;
}

void draw_floating(PixelBuffer& target, const FloatingContents& contents, RectI region) {
    if (contents.empty()) return;
    region = region.intersected(target.bounds());
    if (region.empty()) return;
    fill_from_bytes(target, contents.origin, contents.origin_mask, region, contents.fill);
    stamp_from_bytes(target, contents.position, contents.content, contents.mask, region);
}

}  // namespace lsp
