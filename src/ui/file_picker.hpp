#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace lsp {

// The operating system's own file dialogs. Each blocks until the user answers
// and yields nothing when they cancel.

// False when the platform has no dialog to show.
[[nodiscard]] bool file_dialogs_available();

// Asks for an existing image file, starting in `directory`.
[[nodiscard]] std::optional<std::filesystem::path> pick_image_to_open(const std::string& title,
                                                                      const std::filesystem::path& directory);

// Asks where to write an image, starting in `directory` with `file_name`
// filled in.
[[nodiscard]] std::optional<std::filesystem::path> pick_image_to_save(const std::filesystem::path& directory,
                                                                      const std::string& file_name);

}  // namespace lsp
