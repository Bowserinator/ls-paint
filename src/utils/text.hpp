#pragma once

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace lsp {

[[nodiscard]] inline char to_lower(char letter) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
}

[[nodiscard]] inline char to_upper(char letter) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
}

[[nodiscard]] inline std::string lowered(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](char letter) { return to_lower(letter); });
    return result;
}

[[nodiscard]] inline std::string uppered(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](char letter) { return to_upper(letter); });
    return result;
}

[[nodiscard]] inline bool equals_ignore_case(std::string_view left, std::string_view right) {
    return std::ranges::equal(left, right, {}, to_lower, to_lower);
}

[[nodiscard]] inline std::string_view trimmed(std::string_view text) {
    const auto is_space = [](char letter) {
        return std::isspace(static_cast<unsigned char>(letter)) != 0;
    };
    while (!text.empty() && is_space(text.front())) text.remove_prefix(1);
    while (!text.empty() && is_space(text.back())) text.remove_suffix(1);
    return text;
}

// The whole of `text` read as a decimal, or nothing when any of it is not part
// of the number or the number does not fit.
[[nodiscard]] inline std::optional<int> parse_int(std::string_view text) {
    int value = 0;
    const char* end = text.data() + text.size();
    const std::from_chars_result parsed = std::from_chars(text.data(), end, value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != end) return std::nullopt;
    return value;
}

}  // namespace lsp
