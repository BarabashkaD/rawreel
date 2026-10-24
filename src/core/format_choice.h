#pragma once

// The format dialog's model (design M3 v2 D21): what the user picks for a source whose format is
// unknown or wrong, and how the pick is written into the source URI.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "dvv/base/error.h"
#include "dvv/media/source.h"
#include "dvv/media/source_factory.h"
#include "dvv/media/types.h"

namespace rawreel {

struct FormatChoice {
    std::string format = "yuv420p";  // catalogue name or "@<UTF-8 path to .json>"
    std::uint32_t width = 1920, height = 1080;
    dvv::media::Rational fps{25, 1};
    dvv::media::ColourParams colour{};  // Unspecified fields: the source decides
    bool operator==(const FormatChoice&) const = default;
};

[[nodiscard]] FormatChoice choiceFromCandidate(const dvv::media::Candidate& candidate);
[[nodiscard]] FormatChoice choiceFromInfo(const dvv::media::SourceInfo& info);
// Removes fmt, size, w, h, fps, matrix, kr, kb, range, transfer, primaries from the URI's query,
// keeps every other parameter in order, then appends fmt, size (WxH), fps (N or N/D), and the
// colour fields that are not Unspecified: matrix (with kr and kb for Custom), range, transfer,
// primaries. Formats with dvv::formatUri. Errc::Format for an unparsable URI or an invalid choice.
[[nodiscard]] dvv::Result<std::string> applyChoice(std::string_view uri,
                                                   const FormatChoice& choice);
// Errc::Format: catalogue name unknown; geometry() of the format at the size fails; "@" with an
// empty path; width or height 0; fps num <= 0 or den <= 0; validateColour() fails.
[[nodiscard]] dvv::Result<void> validateChoice(const FormatChoice& choice);
// The format dialog's fps field: "25" -> 25/1, "30000/1001", "29.97" -> 2997/100 (up to 6
// decimals); reduced by the gcd. nullopt for empty text, 0, negatives, a zero denominator, an
// overflow or any other character.
[[nodiscard]] std::optional<dvv::media::Rational> parseFpsText(std::string_view text) noexcept;

}  // namespace rawreel
