#pragma once

#include <charconv>
#include <concepts>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>

namespace rawreel {

// A decimal integer that is the whole text: no spaces, no '+', a leading '-' only for a signed T
// (std::from_chars). nullopt for an empty text, a trailing character or a value outside T.
template <std::integral T>
[[nodiscard]] std::optional<T> parseDecimal(std::string_view text) noexcept {
    if (text.empty()) {
        return std::nullopt;
    }
    T value{};
    const char* const first = std::to_address(text.begin());
    const char* const end = std::to_address(text.end());
    const auto [last, error] = std::from_chars(first, end, value);
    if (error != std::errc{} || last != end) {
        return std::nullopt;
    }
    return value;
}

}  // namespace rawreel
