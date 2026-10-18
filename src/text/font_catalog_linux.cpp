#include "text/font_catalog.hpp"

#include <algorithm>
#include <string>

#include <fontconfig/fontconfig.h>

namespace lsp {
namespace {

// Reads one string value out of a pattern, empty when it holds none.
std::string pattern_string(FcPattern* pattern, const char* object, int index) {
    FcChar8* value = nullptr;
    if (pattern == nullptr || FcPatternGetString(pattern, object, index, &value) != FcResultMatch ||
        value == nullptr) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(value));
}

// The families fontconfig knows, sorted by family key and with the spellings
// fontconfig repeats removed. The first file it reports for a family is the one
// the text tool rasterizes from.
std::vector<FontFace> load_faces() {
    FcPattern* query = FcPatternCreate();
    FcObjectSet* objects = FcObjectSetBuild(FC_FAMILY, FC_FILE, nullptr);
    FcFontSet* set = FcFontList(nullptr, query, objects);

    std::vector<FontFace> faces;
    if (set != nullptr) {
        for (int index = 0; index < set->nfont; ++index) {
            FcPattern* pattern = set->fonts[index];
            const std::string family = pattern_string(pattern, FC_FAMILY, 0);
            if (family.empty()) continue;
            const bool known = std::ranges::any_of(
                faces,
                [&family](const FontFace& face) { return same_font_family(face.family, family); });
            if (!known) faces.push_back({family, pattern_string(pattern, FC_FILE, 0)});
        }
        FcFontSetDestroy(set);
    }
    FcObjectSetDestroy(objects);
    FcPatternDestroy(query);

    sort_faces(faces);
    return faces;
}

class FontconfigCatalog final : public FontCatalog {
public:
    FontconfigCatalog() : m_faces(load_faces()) {}

    [[nodiscard]] std::vector<FontFace> faces() const override { return m_faces; }

    [[nodiscard]] std::string face_path(std::string_view family) const override {
        const std::string name(family);
        FcPattern* query = FcPatternCreate();
        FcPatternAddString(query, FC_FAMILY, reinterpret_cast<const FcChar8*>(name.c_str()));
        FcConfigSubstitute(nullptr, query, FcMatchPattern);
        FcDefaultSubstitute(query);

        FcResult result = FcResultNoMatch;
        FcPattern* match = FcFontMatch(nullptr, query, &result);
        std::string path;
        if (match != nullptr) {
            // Fontconfig answers with its closest family even when none matched,
            // so the answer only counts when it names the family that was asked
            // for.
            if (same_font_family(pattern_string(match, FC_FAMILY, 0), family)) {
                path = pattern_string(match, FC_FILE, 0);
            }
            FcPatternDestroy(match);
        }
        FcPatternDestroy(query);
        return path;
    }

    [[nodiscard]] std::string face_path_covering(char32_t codepoint) const override {
        FcCharSet* wanted = FcCharSetCreate();
        FcCharSetAddChar(wanted, static_cast<FcChar32>(codepoint));
        FcPattern* query = FcPatternCreate();
        FcPatternAddCharSet(query, FC_CHARSET, wanted);
        FcCharSetDestroy(wanted);
        FcConfigSubstitute(nullptr, query, FcMatchPattern);
        FcDefaultSubstitute(query);

        FcResult result = FcResultNoMatch;
        FcPattern* match = FcFontMatch(nullptr, query, &result);
        std::string path;
        if (match != nullptr) {
            // The closest font is returned even when it lacks the character.
            FcCharSet* covered = nullptr;
            if (FcPatternGetCharSet(match, FC_CHARSET, 0, &covered) == FcResultMatch &&
                FcCharSetHasChar(covered, static_cast<FcChar32>(codepoint))) {
                path = pattern_string(match, FC_FILE, 0);
            }
            FcPatternDestroy(match);
        }
        FcPatternDestroy(query);
        return path;
    }

private:
    std::vector<FontFace> m_faces;
};

}  // namespace

const FontCatalog& installed_fonts() {
    static const FontconfigCatalog catalog;
    return catalog;
}

}  // namespace lsp
