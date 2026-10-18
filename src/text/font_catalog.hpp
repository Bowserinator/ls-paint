#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// The installed fonts, whichever platform is running.
//
// The text tool rasterizes glyphs itself, so it needs the file that holds a
// family rather than a system text shaper. Each platform supplies its own
// catalog: fontconfig on Linux and DirectWrite on Windows.
namespace lsp {

// One installed family and the file that holds it. An empty path means the
// platform reports the family but not where it is stored.
struct FontFace {
    std::string family;
    std::string path;
};

// Family names compare without regard to case, so "dejavu sans" and "DejaVu
// Sans" name the same family.
[[nodiscard]] bool same_font_family(std::string_view left, std::string_view right);

// The spelling a family name sorts and deduplicates by.
[[nodiscard]] std::string font_family_key(std::string_view family);

// Puts the faces in the order of their family keys.
void sort_faces(std::vector<FontFace>& faces);

class FontCatalog {
public:
    virtual ~FontCatalog() = default;

    // Every installed face, in the order this catalog reports it.
    [[nodiscard]] virtual std::vector<FontFace> faces() const = 0;

    // The file that holds `family`, empty when the platform knows no such
    // family. A match is only accepted when it names the requested family, so
    // a substitution the platform makes on its own is never mistaken for a hit.
    [[nodiscard]] virtual std::string face_path(std::string_view family) const = 0;

    // The file of an installed face that has a glyph for `codepoint`, empty
    // when no installed face does.
    [[nodiscard]] virtual std::string face_path_covering(char32_t codepoint) const = 0;
};

// The catalog of the running platform.
[[nodiscard]] const FontCatalog& installed_fonts();

}  // namespace lsp
