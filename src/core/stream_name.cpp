#include "core/stream_name.h"

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "dvv/base/error.h"
#include "dvv/base/uri.h"

namespace rawreel {
namespace {

bool isSeparator(char c) {
    return c == '/' || c == '\\';
}

std::string baseNameOf(std::string_view path) {
    const std::size_t cut = path.find_last_of("/\\");
    return std::string(cut == std::string_view::npos ? path : path.substr(cut + 1));
}

bool isDriveLetter(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

// The prefix length of an absolute path in the style (0 when the path is relative).
std::size_t rootLength(std::string_view path, PathStyle style) {
    if (style == PathStyle::Posix) {
        return !path.empty() && path[0] == '/' ? 1 : 0;
    }
    if (path.size() >= 2 && isSeparator(path[0]) && isSeparator(path[1])) {
        return 2;
    }
    if (path.size() >= 3 && isDriveLetter(path[0]) && path[1] == ':' && isSeparator(path[2])) {
        return 3;
    }
    return 0;
}

// Joins the base folder and the path, resolves '.' and '..', writes the style's separator.
std::string absolutePath(std::string_view path, std::string_view base, PathStyle style) {
    std::string joined;
    if (rootLength(path, style) == 0) {
        joined = std::string(base);
        if (!joined.empty() && !isSeparator(joined.back())) {
            joined += '/';
        }
        joined += path;
    } else {
        joined = std::string(path);
    }
    const std::size_t root = rootLength(joined, style);
    const std::string root_text = joined.substr(0, root);
    const char* const separators = style == PathStyle::Windows ? "/\\" : "/";
    std::vector<std::string> parts;
    std::size_t pos = root;
    while (pos <= joined.size()) {
        std::size_t end = joined.find_first_of(separators, pos);
        if (end == std::string::npos) {
            end = joined.size();
        }
        const std::string part = joined.substr(pos, end - pos);
        if (part == "..") {
            if (!parts.empty()) {
                parts.pop_back();
            }
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        pos = end + 1;
    }
    const char sep = style == PathStyle::Windows ? '\\' : '/';
    std::string out;
    for (const char c : root_text) {
        out += isSeparator(c) ? sep : c;
    }
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out += sep;
        }
        out += parts[i];
    }
    return out;
}

}  // namespace

std::string streamName(std::string_view resolved_uri) {
    const dvv::Result<dvv::Uri> parsed = dvv::parseUri(std::string(resolved_uri));
    if (!parsed) {
        return std::string(resolved_uri);
    }
    if (parsed->scheme == "file" && !parsed->path.empty()) {
        return baseNameOf(parsed->path);
    }
    if (!parsed->path.empty()) {
        return parsed->scheme + ":" + parsed->path;
    }
    if (!parsed->body.empty()) {
        return parsed->scheme + ":" + parsed->body;
    }
    return std::string(resolved_uri);
}

std::string streamFullPath(std::string_view resolved_uri, std::string_view base_folder,
                           PathStyle style) {
    const dvv::Result<dvv::Uri> parsed = dvv::parseUri(std::string(resolved_uri));
    if (parsed && parsed->scheme == "file" && !parsed->path.empty()) {
        return absolutePath(parsed->path, base_folder, style);
    }
    return std::string(resolved_uri);
}

std::string windowTitle(std::string_view name, TitleStyle style) {
    if (name.empty()) {
        return "rawreel";
    }
    switch (style) {
        case TitleStyle::MacOs: return std::string(name);
        case TitleStyle::Windows: return std::string(name) + " - rawreel";
        case TitleStyle::Linux: return std::string(name) + " \xE2\x80\x94 rawreel";
    }
    return std::string(name);
}

}  // namespace rawreel
