#pragma once

#include <filesystem>
#include <optional>

namespace lsp {

// The file named by the first command line argument, if there is one. File
// names keep their full Unicode spelling on every platform.
[[nodiscard]] std::optional<std::filesystem::path> startup_file(int argc, char** argv);

}  // namespace lsp
