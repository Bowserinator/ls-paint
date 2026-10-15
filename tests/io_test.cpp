#include <cstdio>
#include <filesystem>
#include <string>

#include "core/canvas_document.hpp"
#include "io/document_io.hpp"

using namespace lsp;

namespace {

int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

std::filesystem::path scratch(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

// A document with a known pattern: red, green, blue, white quadrants.
CanvasDocument make_document() {
    CanvasDocument document(8, 8);
    PixelBuffer& pixels = document.active_layer().pixels;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            if (y < 4) pixels.at(x, y) = x < 4 ? Rgba8{255, 0, 0, 255} : Rgba8{0, 255, 0, 255};
            else pixels.at(x, y) = x < 4 ? Rgba8{0, 0, 255, 255} : Rgba8{255, 255, 255, 255};
        }
    }
    document.mark_dirty(document.bounds());
    (void)document.flush();
    return document;
}

void test_format_from_extension_is_case_insensitive() {
    CHECK(format_from_extension("a.png") == ImageFormat::Png);
    CHECK(format_from_extension("a.PNG") == ImageFormat::Png);
    CHECK(format_from_extension("/tmp/dir/b.JpG") == ImageFormat::Jpeg);
    CHECK(format_from_extension("c.bmp") == ImageFormat::Bmp);
    CHECK(format_from_extension("noextension") == std::nullopt);
    CHECK(format_from_extension("notes.txt") == std::nullopt);
}

void test_format_lookup_helpers_round_trip() {
    CHECK(std::string(extension_for(ImageFormat::Png)) == ".png");
    CHECK(std::string(extension_for(ImageFormat::Bmp)) == ".bmp");
    CHECK(std::string(extension_for(ImageFormat::Jpeg)) == ".jpg");
    for (const ImageFormat format : {ImageFormat::Png, ImageFormat::Bmp, ImageFormat::Jpeg}) {
        CHECK(format_from_index(index_from(format)) == format);
    }
    // An out of range index falls back to Png instead of mis-indexing.
    CHECK(format_from_index(99) == ImageFormat::Png);
}

void test_flatten_to_opaque_blends_and_drops_alpha() {
    PixelBuffer mixed(3, 1);
    mixed.at(0, 0) = Rgba8{0, 0, 0, 0};
    mixed.at(1, 0) = Rgba8{0, 0, 0, 128};
    mixed.at(2, 0) = Rgba8{10, 20, 30, 255};
    const PixelBuffer flat = flatten_to_opaque(mixed);
    CHECK(flat.at(0, 0).r == 255 && flat.at(0, 0).a == 255);
    CHECK(flat.at(1, 0).r == 127 || flat.at(1, 0).r == 128);
    CHECK(flat.at(2, 0).r == 10 && flat.at(2, 0).g == 20 && flat.at(2, 0).b == 30);
}

void test_png_round_trip_keeps_pixels() {
    const CanvasDocument document = make_document();
    const std::filesystem::path path = scratch("ls_paint_io_test.png");
    std::string error;
    CHECK(save(document, path, ImageFormat::Png, &error));
    CHECK(error.empty());

    const LoadResult loaded = load(path);
    CHECK(loaded.ok);
    CHECK(loaded.error.empty());
    CHECK(loaded.pixels.width() == 8 && loaded.pixels.height() == 8);
    CHECK(loaded.pixels.at(0, 0).r == 255 && loaded.pixels.at(0, 0).a == 255);
    CHECK(loaded.pixels.at(7, 0).g == 255);
    CHECK(loaded.pixels.at(0, 7).b == 255);
    std::filesystem::remove(path);
}

void test_bmp_round_trip_flattens_translucent_pixels() {
    CanvasDocument document(4, 1);
    document.active_layer().pixels.at(0, 0) = Rgba8{0, 0, 0, 0};
    document.active_layer().pixels.at(1, 0) = Rgba8{255, 0, 0, 255};
    document.mark_dirty(document.bounds());
    (void)document.flush();

    const std::filesystem::path path = scratch("ls_paint_io_test.bmp");
    std::string error;
    CHECK(save(document, path, ImageFormat::Bmp, &error));

    const LoadResult loaded = load(path);
    CHECK(loaded.ok);
    CHECK(loaded.pixels.width() == 4 && loaded.pixels.height() == 1);
    // A transparent pixel becomes white in a format without alpha.
    CHECK(loaded.pixels.at(0, 0).r == 255 && loaded.pixels.at(0, 0).g == 255);
    CHECK(loaded.pixels.at(0, 0).a == 255);
    CHECK(loaded.pixels.at(1, 0).r == 255 && loaded.pixels.at(1, 0).g == 0);
    std::filesystem::remove(path);
}

void test_jpeg_round_trip_keeps_the_size() {
    const CanvasDocument document = make_document();
    const std::filesystem::path path = scratch("ls_paint_io_test.jpg");
    std::string error;
    CHECK(save(document, path, ImageFormat::Jpeg, &error));

    const LoadResult loaded = load(path);
    CHECK(loaded.ok);
    CHECK(loaded.pixels.width() == 8 && loaded.pixels.height() == 8);
    // JPEG is lossy, so only the rough colour survives.
    CHECK(loaded.pixels.at(1, 1).r > 200 && loaded.pixels.at(1, 1).g < 60);
    std::filesystem::remove(path);
}

void test_load_reports_missing_and_undecodable_files() {
    const LoadResult missing = load(scratch("ls_paint_io_test_absent.png"));
    CHECK(!missing.ok);
    CHECK(!missing.error.empty());

    const std::filesystem::path text = scratch("ls_paint_io_test.txt");
    std::FILE* file = std::fopen(text.string().c_str(), "wb");
    if (file != nullptr) {
        std::fputs("not an image at all", file);
        std::fclose(file);
    }
    const LoadResult broken = load(text);
    CHECK(!broken.ok);
    CHECK(!broken.error.empty());
    std::filesystem::remove(text);
}

void test_save_reports_a_missing_directory() {
    const CanvasDocument document = make_document();
    std::string error;
    const bool ok = save(document, "/tmp/ls_paint_missing_dir/image.png", ImageFormat::Png, &error);
    CHECK(!ok);
    CHECK(!error.empty());
}

}  // namespace

int main() {
    test_format_from_extension_is_case_insensitive();
    test_format_lookup_helpers_round_trip();
    test_flatten_to_opaque_blends_and_drops_alpha();
    test_png_round_trip_keeps_pixels();
    test_bmp_round_trip_flattens_translucent_pixels();
    test_jpeg_round_trip_keeps_the_size();
    test_load_reports_missing_and_undecodable_files();
    test_save_reports_a_missing_directory();

    std::printf(failures == 0 ? "ALL OK\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
