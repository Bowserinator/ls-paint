#include "text/font_catalog.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <dwrite.h>

namespace lsp {
namespace {

// Holds one COM reference for the lifetime of the value.
template <typename Interface>
class ComRef {
public:
    ComRef() = default;
    ComRef(const ComRef&) = delete;
    ComRef& operator=(const ComRef&) = delete;
    ~ComRef() {
        if (m_pointer != nullptr) m_pointer->Release();
    }

    [[nodiscard]] Interface* operator->() const noexcept { return m_pointer; }
    [[nodiscard]] Interface* get() const noexcept { return m_pointer; }
    [[nodiscard]] Interface** put() noexcept { return &m_pointer; }

private:
    Interface* m_pointer = nullptr;
};

std::string utf8_from_wide(std::wstring_view text) {
    if (text.empty()) return {};
    const int length = static_cast<int>(text.size());
    const int size =
        ::WideCharToMultiByte(CP_UTF8, 0, text.data(), length, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.data(), length, result.data(), size, nullptr, nullptr);
    return result;
}

// The name DirectWrite shows for a family, in English when it offers one.
std::wstring family_name(IDWriteLocalizedStrings* names) {
    UINT32 index = 0;
    BOOL found = FALSE;
    if (names->FindLocaleName(L"en-us", &index, &found) != S_OK || found == FALSE) index = 0;
    UINT32 length = 0;
    if (names->GetStringLength(index, &length) != S_OK) return {};
    std::wstring name(static_cast<std::size_t>(length) + 1, L'\0');
    if (names->GetString(index, name.data(), length + 1) != S_OK) return {};
    name.resize(length);
    return name;
}

// The file that holds a font, empty when the platform does not let the file be
// located.
std::string font_file(IDWriteFont* font) {
    ComRef<IDWriteFontFace> face;
    if (font->CreateFontFace(face.put()) != S_OK) return {};

    UINT32 count = 0;
    if (face->GetFiles(&count, nullptr) != S_OK || count == 0) return {};
    std::vector<IDWriteFontFile*> files(count, nullptr);
    if (face->GetFiles(&count, files.data()) != S_OK) {
        for (IDWriteFontFile* file : files) {
            if (file != nullptr) file->Release();
        }
        return {};
    }

    std::string path;
    for (IDWriteFontFile* file : files) {
        if (file == nullptr) continue;
        const void* key = nullptr;
        UINT32 key_size = 0;
        if (file->GetReferenceKey(&key, &key_size) != S_OK) continue;
        ComRef<IDWriteFontFileLoader> loader;
        if (file->GetLoader(loader.put()) != S_OK) continue;
        ComRef<IDWriteLocalFontFileLoader> local;
        if (loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader),
                                   reinterpret_cast<void**>(local.put())) != S_OK) {
            continue;
        }
        UINT32 length = 0;
        if (local->GetFilePathLengthFromKey(key, key_size, &length) != S_OK) continue;
        std::wstring wide(static_cast<std::size_t>(length) + 1, L'\0');
        if (local->GetFilePathFromKey(key, key_size, wide.data(), length + 1) != S_OK) continue;
        wide.resize(length);
        path = utf8_from_wide(wide);
        if (!path.empty()) break;
    }
    for (IDWriteFontFile* file : files) {
        if (file != nullptr) file->Release();
    }
    return path;
}

// Every family DirectWrite reports, with the file of the first font in it. The
// names DirectWrite returns are already unique, so no two entries share a
// family.
std::vector<FontFace> load_faces(IDWriteFontCollection* collection) {
    std::vector<FontFace> faces;
    if (collection == nullptr) return faces;
    const UINT32 family_count = collection->GetFontFamilyCount();
    for (UINT32 index = 0; index < family_count; ++index) {
        ComRef<IDWriteFontFamily> family;
        if (collection->GetFontFamily(index, family.put()) != S_OK) continue;
        ComRef<IDWriteLocalizedStrings> names;
        if (family->GetFamilyNames(names.put()) != S_OK) continue;
        const std::string name = utf8_from_wide(family_name(names.get()));
        if (name.empty()) continue;

        ComRef<IDWriteFont> font;
        if (family->GetFont(0, font.put()) != S_OK) continue;
        faces.push_back({name, font_file(font.get())});
    }

    sort_faces(faces);
    return faces;
}

class DirectWriteCatalog final : public FontCatalog {
public:
    DirectWriteCatalog() {
        ComRef<IDWriteFactory> factory;
        if (DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                reinterpret_cast<IUnknown**>(factory.put())) != S_OK) {
            return;
        }
        if (factory->GetSystemFontCollection(m_collection.put()) != S_OK) return;
        m_faces = load_faces(m_collection.get());
    }

    [[nodiscard]] std::vector<FontFace> faces() const override { return m_faces; }

    [[nodiscard]] std::string face_path(std::string_view family) const override {
        const auto match = std::ranges::find_if(
            m_faces, [family](const FontFace& face) { return same_font_family(face.family, family); });
        return match == m_faces.end() ? std::string{} : match->path;
    }

    [[nodiscard]] std::string face_path_covering(char32_t codepoint) const override {
        if (m_collection.get() == nullptr) return {};
        const UINT32 family_count = m_collection->GetFontFamilyCount();
        for (UINT32 index = 0; index < family_count; ++index) {
            ComRef<IDWriteFontFamily> family;
            if (m_collection->GetFontFamily(index, family.put()) != S_OK) continue;
            ComRef<IDWriteFont> font;
            if (family->GetFont(0, font.put()) != S_OK) continue;
            BOOL has_glyph = FALSE;
            if (font->HasCharacter(static_cast<UINT32>(codepoint), &has_glyph) != S_OK ||
                has_glyph == FALSE) {
                continue;
            }
            std::string path = font_file(font.get());
            if (!path.empty()) return path;
        }
        return {};
    }

private:
    ComRef<IDWriteFontCollection> m_collection;
    std::vector<FontFace> m_faces;
};

}  // namespace

const FontCatalog& installed_fonts() {
    static const DirectWriteCatalog catalog;
    return catalog;
}

}  // namespace lsp
