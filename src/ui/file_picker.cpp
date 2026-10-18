#include "ui/file_picker.hpp"

#include <vector>

#include "portable-file-dialogs.h"

namespace lsp {

namespace {

// A trailing separator makes the dialog treat the path as a folder to open in
// rather than a file to select.
std::string folder_hint(const std::filesystem::path& directory) { return (directory / "").string(); }

}  // namespace

bool file_dialogs_available() { return pfd::settings::available(); }

std::optional<std::filesystem::path> pick_image_to_open(const std::string& title,
                                                        const std::filesystem::path& directory) {
    const std::vector<std::string> filters{"Images", "*.png *.jpg *.jpeg *.bmp", "All Files", "*"};
    const std::vector<std::string> chosen = pfd::open_file(title, folder_hint(directory), filters).result();
    if (chosen.empty()) return std::nullopt;
    return std::filesystem::path(chosen.front());
}

std::optional<std::filesystem::path> pick_image_to_save(const std::filesystem::path& directory,
                                                        const std::string& file_name) {
    const std::vector<std::string> filters{"PNG", "*.png", "BMP", "*.bmp", "JPEG", "*.jpg *.jpeg"};
    const std::string chosen = pfd::save_file("Save As", (directory / file_name).string(), filters).result();
    if (chosen.empty()) return std::nullopt;
    return std::filesystem::path(chosen);
}

}  // namespace lsp
