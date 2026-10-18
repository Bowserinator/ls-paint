#include "ui/file_picker.hpp"

#include <string>
#include <vector>

#include "config.hpp"
#include "portable-file-dialogs.h"
#include "utils/path_text.hpp"

namespace lsp {

namespace {

// A trailing separator makes the dialog treat the path as a folder to open in
// rather than a file to select.
std::string folder_hint(const std::filesystem::path& directory) { return utf8_from_path(directory / ""); }

// The extensions of every image file the application reads, as a glob list.
std::string image_glob() {
    std::string glob;
    for (const std::string_view extension : kImageExtensions) {
        if (!glob.empty()) glob += ' ';
        glob += "*" + std::string(extension);
    }
    return glob;
}

}  // namespace

bool file_dialogs_available() { return pfd::settings::available(); }

std::optional<std::filesystem::path> pick_image_to_open(const std::string& title,
                                                        const std::filesystem::path& directory) {
    const std::vector<std::string> filters{"Images", image_glob(), "All Files", "*"};
    const std::vector<std::string> chosen =
        pfd::open_file(title, folder_hint(directory), filters).result();
    if (chosen.empty()) return std::nullopt;
    return path_from_utf8(chosen.front());
}

std::optional<std::filesystem::path> pick_image_to_save(const std::filesystem::path& directory,
                                                        const std::string& file_name) {
    const std::vector<std::string> filters{"PNG", "*.png", "BMP", "*.bmp", "JPEG", "*.jpg *.jpeg"};
    const std::string chosen =
        pfd::save_file("Save As", utf8_from_path(directory / path_from_utf8(file_name)), filters).result();
    if (chosen.empty()) return std::nullopt;
    return path_from_utf8(chosen);
}

}  // namespace lsp
