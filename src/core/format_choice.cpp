#include "core/format_choice.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <numeric>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include "dvv/base/uri.h"
#include "dvv/media/pixel_format.h"

namespace rawreel {

namespace {

using dvv::media::ColourMatrix;
using dvv::media::ColourRange;
using dvv::media::Primaries;
using dvv::media::TransferFn;

// The URI names of the colour fields: a copy of dvv-media's URI parser table, which is private.
template <class E>
struct Name {
    E value;
    std::string_view text;
};
constexpr std::array<Name<ColourMatrix>, 5> MATRICES{{
    {ColourMatrix::Bt601, "bt601"},
    {ColourMatrix::Bt709, "bt709"},
    {ColourMatrix::Bt2020Ncl, "bt2020"},
    {ColourMatrix::Rgb, "rgb"},
    {ColourMatrix::Custom, "custom"},
}};
constexpr std::array<Name<ColourRange>, 2> RANGES{{
    {ColourRange::Limited, "limited"},
    {ColourRange::Full, "full"},
}};
constexpr std::array<Name<TransferFn>, 5> TRANSFERS{{
    {TransferFn::Srgb, "srgb"},
    {TransferFn::Bt1886, "bt1886"},
    {TransferFn::Linear, "linear"},
    {TransferFn::Pq, "pq"},
    {TransferFn::Hlg, "hlg"},
}};
constexpr std::array<Name<Primaries>, 4> PRIMARIES{{
    {Primaries::Bt601_525, "bt601-525"},
    {Primaries::Bt601_625, "bt601-625"},
    {Primaries::Bt709, "bt709"},
    {Primaries::Bt2020, "bt2020"},
}};

template <class E, std::size_t N>
std::string nameOf(E value, const std::array<Name<E>, N>& names) {
    const auto found = std::ranges::find(names, value, &Name<E>::value);
    return found == names.end() ? std::string() : std::string(found->text);
}

// The parameters applyChoice() replaces.
constexpr std::array<std::string_view, 11> REPLACED{
    "fmt", "size", "w", "h", "fps", "matrix", "kr", "kb", "range", "transfer", "primaries"};

std::string formatFps(dvv::media::Rational fps) {
    if (fps.den == 1) {
        return std::format("{}", fps.num);
    }
    return std::format("{}/{}", fps.num, fps.den);
}

constexpr std::size_t MAX_FPS_DECIMALS = 6;

// A non-empty run of ASCII digits as a positive or zero int64; nullopt on anything else or an
// overflow.
std::optional<std::int64_t> parseDigits(std::string_view text) noexcept {
    if (text.empty() || !std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    std::int64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

}  // namespace

FormatChoice choiceFromCandidate(const dvv::media::Candidate& candidate) {
    const dvv::media::FormatGuess& guess = candidate.guess;
    FormatChoice choice;
    if (guess.format) {
        choice.format = dvv::media::descriptor(*guess.format).name;
    }
    if (guess.width != 0 && guess.height != 0) {
        choice.width = guess.width;
        choice.height = guess.height;
    }
    if (guess.fps) {
        choice.fps = *guess.fps;
    }
    return choice;
}

FormatChoice choiceFromInfo(const dvv::media::SourceInfo& info) {
    FormatChoice choice;
    choice.format = dvv::media::descriptor(info.desc.format).name;
    if (const auto uri = dvv::parseUri(info.resolvedUri)) {
        if (const auto fmt = uri->param("fmt"); fmt && fmt->starts_with('@')) {
            choice.format = std::string(*fmt);  // a custom JSON format
        }
    }
    choice.width = info.desc.width;
    choice.height = info.desc.height;
    choice.fps = info.fps;
    choice.colour = dvv::media::colourParams(info.desc.colour);
    return choice;
}

dvv::Result<std::string> applyChoice(std::string_view uri, const FormatChoice& choice) {
    DVV_TRY_VOID(validateChoice(choice));
    auto parsed = dvv::parseUri(uri);
    if (!parsed) {
        return dvv::makeError(dvv::Errc::Format, std::format("cannot apply the format to '{}': {}",
                                                             uri, parsed.error().message));
    }
    if (!parsed->body.empty()) {  // an opaque scheme (e.g. lavfi:) has no format parameters
        return dvv::makeError(dvv::Errc::Format,
                              std::format("cannot apply a format to a '{}:' URI", parsed->scheme));
    }
    std::erase_if(parsed->params, [](const auto& param) {
        return std::ranges::find(REPLACED, param.first) != REPLACED.end();
    });
    auto& params = parsed->params;
    params.emplace_back("fmt", choice.format);
    params.emplace_back("size", std::format("{}x{}", choice.width, choice.height));
    params.emplace_back("fps", formatFps(choice.fps));
    const dvv::media::ColourParams& colour = choice.colour;
    if (colour.matrix != ColourMatrix::Unspecified) {
        params.emplace_back("matrix", nameOf(colour.matrix, MATRICES));
        if (colour.matrix == ColourMatrix::Custom) {
            params.emplace_back("kr", std::format("{}", colour.kr));
            params.emplace_back("kb", std::format("{}", colour.kb));
        }
    }
    if (colour.range != ColourRange::Unspecified) {
        params.emplace_back("range", nameOf(colour.range, RANGES));
    }
    if (colour.transfer != TransferFn::Unspecified) {
        params.emplace_back("transfer", nameOf(colour.transfer, TRANSFERS));
    }
    if (colour.primaries != Primaries::Unspecified) {
        params.emplace_back("primaries", nameOf(colour.primaries, PRIMARIES));
    }
    return dvv::formatUri(*parsed);
}

dvv::Result<void> validateChoice(const FormatChoice& choice) {
    const bool custom_json = choice.format.starts_with('@');
    std::optional<dvv::media::FormatId> format;
    if (custom_json) {
        if (choice.format.size() == 1) {
            return dvv::makeError(dvv::Errc::Format, "the custom format needs a JSON file path");
        }
    } else {
        format = dvv::media::findFormat(choice.format);
        if (!format) {
            return dvv::makeError(dvv::Errc::Format,
                                  std::format("unknown pixel format '{}'", choice.format));
        }
    }
    if (choice.width == 0 || choice.height == 0) {
        return dvv::makeError(dvv::Errc::Format,
                              std::format("bad size {}x{}: width and height must "
                                          "not be 0",
                                          choice.width, choice.height));
    }
    if (choice.fps.num <= 0 || choice.fps.den <= 0) {
        return dvv::makeError(dvv::Errc::Format,
                              std::format("bad fps {}/{}", choice.fps.num, choice.fps.den));
    }
    if (format) {
        // The JSON of a custom format is loaded when the URI is opened.
        const auto geometry = dvv::media::geometry(*format, choice.width, choice.height);
        if (!geometry) {
            return dvv::makeError(dvv::Errc::Format, geometry.error().message);
        }
    }
    if (auto valid = dvv::media::validateColour(choice.colour); !valid) {
        return dvv::makeError(dvv::Errc::Format, valid.error().message);
    }
    return {};
}

std::optional<dvv::media::Rational> parseFpsText(std::string_view text) noexcept {
    std::int64_t num = 0;
    std::int64_t den = 1;
    if (const std::size_t slash = text.find('/'); slash != std::string_view::npos) {
        const std::optional<std::int64_t> n = parseDigits(text.substr(0, slash));
        const std::optional<std::int64_t> d = parseDigits(text.substr(slash + 1));
        if (!n || !d) {
            return std::nullopt;
        }
        num = *n;
        den = *d;
    } else {
        const std::size_t dot = text.find('.');
        const std::string_view whole = text.substr(0, dot);
        const std::string_view decimals =
            dot == std::string_view::npos ? std::string_view() : text.substr(dot + 1);
        if (dot != std::string_view::npos &&
            (decimals.empty() || decimals.size() > MAX_FPS_DECIMALS)) {
            return std::nullopt;
        }
        const std::optional<std::int64_t> w = parseDigits(whole);
        const std::optional<std::int64_t> f =
            decimals.empty() ? std::optional<std::int64_t>(0) : parseDigits(decimals);
        if (!w || !f) {
            return std::nullopt;
        }
        for (std::size_t i = 0; i < decimals.size(); ++i) {
            den *= 10;  // at most 10^6
        }
        if (*w > (std::numeric_limits<std::int64_t>::max() - *f) / den) {
            return std::nullopt;
        }
        num = *w * den + *f;
    }
    if (num <= 0 || den <= 0) {
        return std::nullopt;
    }
    const std::int64_t divisor = std::gcd(num, den);
    return dvv::media::Rational{num / divisor, den / divisor};
}

}  // namespace rawreel
