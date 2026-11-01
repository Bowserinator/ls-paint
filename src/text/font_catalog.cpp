#include "text/font_catalog.hpp"

#include <algorithm>

#include "utils/text.hpp"

namespace lsp {

bool same_font_family(std::string_view left, std::string_view right) {
    return equals_ignore_case(left, right);
}

std::string font_family_key(std::string_view family) { return lowered(family); }

void sort_faces(std::vector<FontFace>& faces) {
    std::ranges::sort(faces, [](const FontFace& left, const FontFace& right) {
        return font_family_key(left.family) < font_family_key(right.family);
    });
}

}  // namespace lsp
