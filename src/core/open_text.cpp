#include "core/open_text.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "dvv/base/uri.h"
#include "dvv/media/source_factory.h"
#include "dvv/media/types.h"

namespace rawreel {

namespace {

constexpr std::string_view SCHEME_HINT =
    "; no scheme: opened as a file path; a URI needs e.g. synthetic: or lavfi:";

// Files the Y4M or the ffmpeg provider recognises: the raw format dialog cannot
// fix their open errors. Lower case, without the dot. Includes the common elementary-stream
// extensions; deciding by the provider that failed is the v1.x form.
constexpr std::array<std::string_view, 31> CONTAINER_EXTENSIONS{
    "y4m",  "avi", "mkv", "mka", "mov", "mp4",  "m4v",  "nut",  "webm", "mxf", "ts",
    "m2ts", "mts", "flv", "asf", "wmv", "mpg",  "mpeg", "vob",  "3gp",  "3g2", "ogv",
    "ogg",  "ivf", "dv",  "f4v", "wtv", "h264", "264",  "hevc", "265"};

[[nodiscard]] constexpr bool isAlpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

[[nodiscard]] constexpr bool isSchemeChar(char c) noexcept {
    return isAlpha(c) || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
}

}  // namespace

std::filesystem::path uriFolder(std::string_view text) {
    const dvv::Result<dvv::Uri> uri = dvv::parseUri(text);
    if (!uri || uri->scheme != "file" || uri->path.empty()) {
        return {};
    }
    // Absolute against the working directory: a bare name or a relative folder is no start folder
    // the file dialog can use.
    std::error_code ec;
    std::filesystem::path path = std::filesystem::absolute(dvv::media::pathFromUtf8(uri->path), ec);
    return ec ? std::filesystem::path{} : path.parent_path();
}

std::string cleanUriText(std::string_view text) {
    const auto space = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
    };
    while (!text.empty() && space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && space(text.back())) {
        text.remove_suffix(1);
    }
    return withoutLineBreaks(text);
}

std::string withoutLineBreaks(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char c : text) {
        if (c != '\r' && c != '\n') {
            result += c;
        }
    }
    return result;
}

bool hasScheme(std::string_view text) noexcept {
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos || colon < 2 || !isAlpha(text[0])) {
        return false;
    }
    for (const char c : text.substr(0, colon)) {
        if (!isSchemeChar(c)) {
            return false;
        }
    }
    return true;
}

std::string withSchemeHint(std::string text, std::string_view typed, const dvv::Error& error) {
    // A separator before the first '=', '&' or '?' marks a file path such as "X:\R&D\clip.yuv".
    const std::size_t query = typed.find_first_of("=&?");
    if (error.code == dvv::Errc::Io && !hasScheme(typed) && query != std::string_view::npos &&
        typed.substr(0, query).find_first_of("/\\") == std::string_view::npos) {
        text += SCHEME_HINT;
    }
    return text;
}

bool isContainerName(std::string_view path) {
    const std::size_t dot = path.rfind('.');
    if (dot == std::string_view::npos || path.find_first_of("/\\", dot) != std::string_view::npos) {
        return false;
    }
    std::string extension;
    for (const char c : path.substr(dot + 1)) {
        extension += (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    return std::ranges::find(CONTAINER_EXTENSIONS, extension) != CONTAINER_EXTENSIONS.end();
}

bool offersFormat(std::string_view uri, const dvv::Error& error) {
    if (error.code != dvv::Errc::Format && !dvv::media::isNoProviderMatch(error)) {
        return false;
    }
    const dvv::Result<dvv::Uri> parsed = dvv::parseUri(uri);
    if (!parsed || parsed->scheme != "file" || parsed->path.empty()) {
        return false;
    }
    if (const std::optional<std::string_view> impl = parsed->param("impl")) {
        return *impl == "raw";
    }
    return !isContainerName(parsed->path);
}

std::string withRawImpl(std::string_view uri) {
    dvv::Result<dvv::Uri> parsed = dvv::parseUri(uri);
    if (!parsed || parsed->scheme != "file") {
        return std::string(uri);
    }
    std::erase_if(parsed->params, [](const auto& param) { return param.first == "impl"; });
    parsed->params.emplace_back("impl", "raw");
    return dvv::formatUri(*parsed);
}

}  // namespace rawreel
