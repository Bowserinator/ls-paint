#include "text/font_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>

namespace lsp {

bool same_font_family(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto lower = [](char letter) {
            return static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
        };
        if (lower(left[index]) != lower(right[index])) return false;
    }
    return true;
}

std::string font_family_key(std::string_view family) {
    std::string key(family);
    std::ranges::transform(key, key.begin(), [](char letter) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    });
    return key;
}

}  // namespace lsp
